// AudioService: SD -> minimp3 -> ring PSRAM (mono) -> I2S (MAX98357A).
//
// Hai task o Core 0:
//   - decodeTask: doc lenh (play/stop/seek), doc SD, giai ma, day PCM mono vao ring.
//   - outputTask: lay PCM tu ring, ap am luong, nhan doi ra L=R, ghi I2S (chan theo DMA).
// Ring bao ve bang mutex ngan (mot lan copy toi da ~1152 mau). Khong cham LVGL.
//
// Thu tu khoa (luon theo thu tu nay, khong dao): gCtlMtx -> gRingMtx -> khoa SD (ben trong SdStorage).

#include "services/audio_service.hpp"

#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#include "core/psram.hpp"
#include "services/sd_storage.hpp"

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace {

#include "config/pins.hpp"   // thêm cùng chỗ với các include khác

constexpr int kPinBclk = Pins::I2sBclk;
constexpr int kPinLrc  = Pins::I2sLrc;
constexpr int kPinDin  = Pins::I2sDin;

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr uint32_t kDefaultRate = 44100;

constexpr size_t kRingSamples = 64 * 1024;       // mono int16 = 128 KB, luy thua cua 2
constexpr size_t kRingMask = kRingSamples - 1;
constexpr size_t kInCap = 32 * 1024;             // bo dem nhap MP3 (PSRAM)
constexpr size_t kInRefillBelow = 16 * 1024;     // minimp3 can >= ~16 KB de dong bo frame chac chan
constexpr size_t kReadChunk = 4096;              // giu khoa SD ngan
constexpr size_t kOutFrames = 256;
constexpr size_t kTapSamples = 2048;
constexpr size_t kMaxFrameSamples = 1152;

constexpr uint32_t kDecodeStack = 32 * 1024;     // minimp3 dung ~16 KB stack
constexpr uint32_t kOutputStack = 4 * 1024;
constexpr int kDecodePrio = 3;
constexpr int kOutputPrio = 4;
constexpr int kAudioCore = 0;

// ---- Trang thai chung ----
bool gStarted = false;
SemaphoreHandle_t gCtlMtx = nullptr;
SemaphoreHandle_t gRingMtx = nullptr;

int16_t* gRing = nullptr;
volatile uint32_t gHead = 0;      // chi decodeTask (hoac flush) ghi
volatile uint32_t gTail = 0;      // chi outputTask (hoac flush) ghi
uint32_t gBaseMs = 0;             // moc thoi gian cua mau dau ring (duoi gRingMtx)
uint64_t gSamplesOut = 0;         // so mau da lay ra tu luc moc (duoi gRingMtx)

volatile AudioState gState = AudioState::STOPPED;
volatile bool gDecodeDone = false;   // decode da day het bai vao ring
volatile bool gFinished = false;      // bao ve bang gFinMux
portMUX_TYPE gFinMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t gRate = kDefaultRate;
volatile uint32_t gDurationMs = 0;
volatile uint8_t gVolume = 50;

// Lenh dang cho (duoi gCtlMtx)
char gPendingPath[192];
bool gPendingPlay = false;
bool gPendingStop = false;
int32_t gPendingSeekMs = 0;

// PCM tap
portMUX_TYPE gTapMux = portMUX_INITIALIZER_UNLOCKED;
int16_t gTap[kTapSamples];
uint32_t gTapCount = 0;           // tong mau da ghi

// ---- Ring ----
// Goi khi da giu gCtlMtx hoac trong decodeTask: lay gRingMtx ben trong.
void flushRing(uint32_t baseMs) {
  xSemaphoreTake(gRingMtx, portMAX_DELAY);
  gHead = 0;
  gTail = 0;
  gBaseMs = baseMs;
  gSamplesOut = 0;
  gDecodeDone = false;
  xSemaphoreGive(gRingMtx);
}

size_t ringFree() { return kRingSamples - (size_t)(gHead - gTail); }

void ringPush(const int16_t* s, size_t n) {
  xSemaphoreTake(gRingMtx, portMAX_DELAY);
  size_t pos = gHead & kRingMask;
  size_t first = std::min(n, kRingSamples - pos);
  memcpy(gRing + pos, s, first * sizeof(int16_t));
  if (n > first) memcpy(gRing, s + first, (n - first) * sizeof(int16_t));
  gHead = gHead + (uint32_t)n;
  xSemaphoreGive(gRingMtx);
}

size_t ringPop(int16_t* dst, size_t maxN) {
  xSemaphoreTake(gRingMtx, portMAX_DELAY);
  size_t avail = (size_t)(gHead - gTail);
  size_t n = std::min(avail, maxN);
  if (n > 0) {
    size_t pos = gTail & kRingMask;
    size_t first = std::min(n, kRingSamples - pos);
    memcpy(dst, gRing + pos, first * sizeof(int16_t));
    if (n > first) memcpy(dst + first, gRing, (n - first) * sizeof(int16_t));
    gTail = gTail + (uint32_t)n;
    gSamplesOut += n;
  }
  xSemaphoreGive(gRingMtx);
  return n;
}

// ---- Giai ma ----
struct Track {
  SdStorage::Reader reader;
  mp3dec_t dec;
  uint8_t* in = nullptr;            // PSRAM, kInCap byte
  size_t inPos = 0, inFill = 0;     // du lieu hop le: in[inPos .. inPos+inFill)
  bool eof = false;
  bool open = false;
  bool started = false;             // da giai ma frame dau
  uint32_t dataStart = 0;           // sau ID3v2
  uint32_t dataBytes = 0;
  uint32_t xingFrames = 0;
  uint32_t durMs = 0;
};
Track gTrack;

uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// force = doc du khi inFill >= nguong (dung khi decode bao thieu du lieu).
void refill(Track& t, bool force = false) {
  if (t.eof) return;
  if (!force && t.inFill >= kInRefillBelow) return;
  if (t.inPos > 0) {
    if (t.inFill > 0) memmove(t.in, t.in + t.inPos, t.inFill);
    t.inPos = 0;
  }
  while (t.inFill < kInCap) {
    size_t want = std::min(kReadChunk, kInCap - t.inFill);
    size_t got = t.reader.read(t.in + t.inFill, want);
    if (got == 0) {
      t.eof = true;
      break;
    }
    t.inFill += got;
  }
}

bool openTrack(Track& t, const char* path) {
  t.reader.close();
  t.open = false;
  t.eof = false;
  t.started = false;
  t.inPos = t.inFill = 0;
  t.dataStart = t.dataBytes = t.xingFrames = t.durMs = 0;

  if (!SdStorage::isMounted() || !t.reader.open(path)) return false;
  const size_t fsize = t.reader.size();

  uint8_t hdr[10];
  size_t got = t.reader.read(hdr, sizeof(hdr));
  if (got == sizeof(hdr) && memcmp(hdr, "ID3", 3) == 0) {
    uint32_t sz = ((uint32_t)(hdr[6] & 0x7F) << 21) | ((uint32_t)(hdr[7] & 0x7F) << 14) |
                  ((uint32_t)(hdr[8] & 0x7F) << 7) | (uint32_t)(hdr[9] & 0x7F);
    t.dataStart = 10 + sz + ((hdr[5] & 0x10) ? 10 : 0);   // bit 4 = co footer
  }
  if (t.dataStart >= fsize || !t.reader.seek(t.dataStart)) {
    t.reader.close();
    return false;
  }
  t.dataBytes = (uint32_t)(fsize - t.dataStart);

  mp3dec_init(&t.dec);
  refill(t);
  if (t.inFill == 0) {
    t.reader.close();
    return false;
  }

  // Xing/Info (VBR, hoac CBR co header): lay so frame de tinh thoi luong chinh xac.
  size_t scan = std::min<size_t>(t.inFill, 256);
  for (size_t i = 0; i + 12 <= scan; i++) {
    if (memcmp(t.in + i, "Xing", 4) == 0 || memcmp(t.in + i, "Info", 4) == 0) {
      uint32_t flags = be32(t.in + i + 4);
      if (flags & 1) t.xingFrames = be32(t.in + i + 8);
      break;
    }
  }
  t.open = true;
  return true;
}

void closeTrack(Track& t) {
  t.reader.close();
  t.open = false;
  t.eof = false;
  t.inPos = t.inFill = 0;
}

// Tua den `ms` (uoc luong tuyen tinh theo byte; chinh xac voi CBR).
void seekTrack(Track& t, uint32_t ms) {
  if (!t.open || t.durMs == 0) return;
  if (ms > t.durMs) ms = t.durMs;
  uint64_t off = (uint64_t)ms * t.dataBytes / t.durMs;
  if (!t.reader.seek((size_t)(t.dataStart + off))) return;
  t.eof = false;
  t.inPos = t.inFill = 0;
  mp3dec_init(&t.dec);
  flushRing(ms);
}

enum class Step { WORKED, FINISHED };

// Giai ma mot frame va day vao ring. Goi khi ring con >= kMaxFrameSamples cho trong.
Step decodeStep(Track& t) {
  static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  static int16_t mono[kMaxFrameSamples];

  refill(t);
  mp3dec_frame_info_t info;
  int samples = mp3dec_decode_frame(&t.dec, t.in + t.inPos, (int)t.inFill, pcm, &info);

  if (info.frame_bytes == 0) {
    // Thieu du lieu hoac khong co dong bo trong bo dem.
    if (t.eof) return Step::FINISHED;
    if (t.inFill >= kInCap) {            // day bo dem ma khong co frame: bo nua dau
      size_t drop = t.inFill / 2;
      t.inPos += drop;
      t.inFill -= drop;
    }
    refill(t, true);
    return Step::WORKED;
  }

  t.inPos += (size_t)info.frame_bytes;
  t.inFill -= (size_t)info.frame_bytes;
  if (samples <= 0) return Step::WORKED;   // bo qua rac / frame khong phai audio

  if (!t.started) {
    t.started = true;
    uint32_t hz = (uint32_t)info.hz;
    if (t.xingFrames > 0) {
      uint32_t spf = (hz >= 32000) ? 1152 : 576;
      t.durMs = (uint32_t)((uint64_t)t.xingFrames * spf * 1000 / hz);
    } else if (info.bitrate_kbps > 0) {
      t.durMs = (uint32_t)((uint64_t)t.dataBytes * 8 / (uint32_t)info.bitrate_kbps);
    }
    gDurationMs = t.durMs;
    gRate = hz;   // dat truoc khi day mau dau tien vao ring
  }

  const int ch = info.channels;
  const size_t n = std::min<size_t>((size_t)samples, kMaxFrameSamples);
  if (ch >= 2) {
    for (size_t i = 0; i < n; i++) mono[i] = (int16_t)(((int)pcm[2 * i] + (int)pcm[2 * i + 1]) / 2);
  } else {
    for (size_t i = 0; i < n; i++) mono[i] = pcm[i];
  }
  ringPush(mono, n);
  return Step::WORKED;
}

void decodeTask(void*) {
  for (;;) {
    // --- Nhan lenh (flush ring ngay trong khoa de output khong thay trang thai cu) ---
    char path[sizeof(gPendingPath)];
    bool doPlay, doStop;
    int32_t seekMs;
    xSemaphoreTake(gCtlMtx, portMAX_DELAY);
    doPlay = gPendingPlay;
    doStop = gPendingStop;
    seekMs = gPendingSeekMs;
    if (doPlay) strlcpy(path, gPendingPath, sizeof(path));
    gPendingPlay = gPendingStop = false;
    gPendingSeekMs = 0;
    if (doPlay || doStop) {
      flushRing(0);
      gDurationMs = 0;
    }
    xSemaphoreGive(gCtlMtx);

    if (doStop) closeTrack(gTrack);
    if (doPlay) {
      closeTrack(gTrack);
      if (!openTrack(gTrack, path)) {
        Serial.printf("[Audio] khong mo duoc %s\n", path);
        gState = AudioState::ERROR;
      }
    } else if (seekMs != 0 && gTrack.open && gTrack.durMs > 0) {
      int64_t target = (int64_t)AudioService::positionMs() + seekMs;
      if (target < 0) target = 0;
      seekTrack(gTrack, (uint32_t)target);
    }

    // --- Giai ma ---
    if (!gTrack.open) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (ringFree() < kMaxFrameSamples) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (decodeStep(gTrack) == Step::FINISHED) {
      closeTrack(gTrack);
      gDecodeDone = true;   // outputTask se tu chuyen STOPPED khi ring rong
    }
  }
}

// ---- Xuat I2S ----
void outputTask(void*) {
  static int16_t mono[kOutFrames];
  static int16_t stereo[kOutFrames * 2];
  uint32_t i2sRate = kDefaultRate;
  bool wasPlaying = false;

  for (;;) {
    if (gState != AudioState::PLAYING) {
      if (wasPlaying) {
        i2s_zero_dma_buffer(kPort);
        wasPlaying = false;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    wasPlaying = true;

    size_t n = ringPop(mono, kOutFrames);
    if (n == 0) {
      // Het bai? (decode xong va ring rong)
      if (gDecodeDone && gHead == gTail) {
        vTaskDelay(pdMS_TO_TICKS(80));   // cho DMA phat het phan cuoi
        xSemaphoreTake(gCtlMtx, portMAX_DELAY);
        xSemaphoreTake(gRingMtx, portMAX_DELAY);
        if (!gPendingPlay && !gPendingStop && gState == AudioState::PLAYING && gDecodeDone &&
            gHead == gTail) {
          gState = AudioState::STOPPED;
          portENTER_CRITICAL(&gFinMux);
          gFinished = true;
          portEXIT_CRITICAL(&gFinMux);
          gDecodeDone = false;
        }
        xSemaphoreGive(gRingMtx);
        xSemaphoreGive(gCtlMtx);
      } else {
        vTaskDelay(pdMS_TO_TICKS(1));   // ring doi decode
      }
      continue;
    }

    uint32_t r = gRate;
    if (r != i2sRate && r >= 8000) {
      i2s_set_clk(kPort, r, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
      i2sRate = r;
    }

    // PCM tap (truoc khi ap am luong, de Visualizer khong phu thuoc volume)
    portENTER_CRITICAL(&gTapMux);
    for (size_t i = 0; i < n; i++) gTap[(gTapCount + i) % kTapSamples] = mono[i];
    gTapCount += (uint32_t)n;
    portEXIT_CRITICAL(&gTapMux);

    // Am luong theo duong cong v^2 (100 = nguyen goc), nhan doi mono ra L=R.
    const uint32_t v = gVolume;
    const int32_t gain = (int32_t)((v * v * 256) / 10000);
    for (size_t i = 0; i < n; i++) {
      int16_t s = (int16_t)(((int32_t)mono[i] * gain) >> 8);
      stereo[2 * i] = s;
      stereo[2 * i + 1] = s;
    }
    size_t written = 0;
    i2s_write(kPort, stereo, n * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
  }
}

// Tra lai bo nho cap trong begin() khi begin() that bai (khoa giu lai de dung lai).
void releaseBuffers() {
  Psram::release(gRing);
  Psram::release(gTrack.in);
  gRing = nullptr;
  gTrack.in = nullptr;
}

}  // namespace

// ================= API =================
namespace AudioService {

bool begin() {
  if (gStarted) return true;

  // Khoa tao mot lan duy nhat va dung lai o cac lan begin() sau (khong ro khi begin() that bai roi duoc goi lai).
  if (!gCtlMtx) gCtlMtx = xSemaphoreCreateMutex();
  if (!gRingMtx) gRingMtx = xSemaphoreCreateMutex();
  gRing = static_cast<int16_t*>(Psram::alloc(kRingSamples * sizeof(int16_t)));
  gTrack.in = static_cast<uint8_t*>(Psram::alloc(kInCap));
  if (!gRing || !gTrack.in || !gCtlMtx || !gRingMtx) {
    Serial.println("[Audio] het bo nho");
    releaseBuffers();
    return false;
  }

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kDefaultRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = kPinBclk;
  pins.ws_io_num = kPinLrc;
  pins.data_out_num = kPinDin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("[Audio] loi khoi tao I2S");
    releaseBuffers();
    return false;
  }
  if (i2s_set_pin(kPort, &pins) != ESP_OK) {
    Serial.println("[Audio] loi khoi tao I2S");
    i2s_driver_uninstall(kPort);   // driver da cai: go ra de lan begin() sau cai lai duoc
    releaseBuffers();
    return false;
  }
  i2s_zero_dma_buffer(kPort);

  xTaskCreatePinnedToCore(decodeTask, "audio_dec", kDecodeStack, nullptr, kDecodePrio, nullptr, kAudioCore);
  xTaskCreatePinnedToCore(outputTask, "audio_out", kOutputStack, nullptr, kOutputPrio, nullptr, kAudioCore);
  gStarted = true;
  return true;
}

bool play(const char* path) {
  if (!gStarted || !path || !path[0]) return false;
  xSemaphoreTake(gCtlMtx, portMAX_DELAY);
  strlcpy(gPendingPath, path, sizeof(gPendingPath));
  gPendingPlay = true;
  gPendingStop = false;
  gPendingSeekMs = 0;
  portENTER_CRITICAL(&gFinMux);
  gFinished = false;
  portEXIT_CRITICAL(&gFinMux);
  gState = AudioState::PLAYING;
  xSemaphoreGive(gCtlMtx);
  return true;
}

void pause() {
  if (gState == AudioState::PLAYING) gState = AudioState::PAUSED;
}

void resume() {
  if (gState == AudioState::PAUSED) gState = AudioState::PLAYING;
}

void stop() {
  if (!gStarted) return;
  xSemaphoreTake(gCtlMtx, portMAX_DELAY);
  gPendingStop = true;
  gPendingPlay = false;
  gPendingSeekMs = 0;
  gState = AudioState::STOPPED;
  xSemaphoreGive(gCtlMtx);
}

void seek(int deltaSeconds) {
  if (!gStarted) return;
  // Chan moi lan goi va cong don o +-1 ngay: tranh tran int32 (seek(INT_MAX) hoac bam lien tuc).
  const int32_t kMaxSeekSec = 86400;
  const int32_t s = deltaSeconds > kMaxSeekSec ? kMaxSeekSec : (deltaSeconds < -kMaxSeekSec ? -kMaxSeekSec : deltaSeconds);
  const int64_t lim = (int64_t)kMaxSeekSec * 1000;
  xSemaphoreTake(gCtlMtx, portMAX_DELAY);
  int64_t sum = (int64_t)gPendingSeekMs + (int64_t)s * 1000;
  gPendingSeekMs = (int32_t)(sum > lim ? lim : (sum < -lim ? -lim : sum));
  xSemaphoreGive(gCtlMtx);
}

void setVolume(uint8_t v0to100) { gVolume = v0to100 > 100 ? 100 : v0to100; }

AudioState state() { return gState; }

uint32_t positionMs() {
  if (!gStarted) return 0;
  xSemaphoreTake(gRingMtx, portMAX_DELAY);
  uint32_t rate = gRate ? gRate : kDefaultRate;
  uint64_t ms = (uint64_t)gBaseMs + gSamplesOut * 1000ULL / rate;
  xSemaphoreGive(gRingMtx);
  uint32_t dur = gDurationMs;
  if (dur > 0 && ms > dur) ms = dur;
  return (uint32_t)ms;
}

uint32_t durationMs() { return gDurationMs; }

bool finished() {
  // Doc va xoa co trong mot khoa: decodeTask/outputTask co the dat co dung luc nay.
  portENTER_CRITICAL(&gFinMux);
  bool f = gFinished;
  gFinished = false;
  portEXIT_CRITICAL(&gFinMux);
  return f;
}

size_t readPcmTap(int16_t* dst, size_t n) {
  if (!gStarted || !dst || n == 0 || gState != AudioState::PLAYING) return 0;
  portENTER_CRITICAL(&gTapMux);
  size_t count = std::min<size_t>({n, kTapSamples, (size_t)gTapCount});
  uint32_t start = gTapCount - (uint32_t)count;
  for (size_t i = 0; i < count; i++) dst[i] = gTap[(start + i) % kTapSamples];
  portEXIT_CRITICAL(&gTapMux);
  return count;
}

}  // namespace AudioService