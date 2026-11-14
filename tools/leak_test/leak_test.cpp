// tools/leak_test/leak_test.cpp
// Sketch kiểm thử rò rỉ bộ nhớ cho firmware Cyberdeck. Build bằng env `leak_test`
// (xem đoạn platformio.ini kèm theo). KHÔNG thuộc firmware chính.
//
// GIAI ĐOẠN 0: đo mốc nền ở Main Screen (đứng yên).
// GIAI ĐOẠN 2 (Kind::APP): Main -> app -> Main. Ở lại kDwellMs, về Main, chờ kSettleMs
//   cho task nền kết thúc rồi lấy mẫu.
// GIAI ĐOẠN 3 (Kind::THEME_*): đổi theme qua Themes::select + AppManager::reload().
//   Mỗi "vòng" đi qua TẤT CẢ theme rồi về theme ban đầu (nên mẫu luôn lấy ở cùng theme).
//   Không gọi SettingsStore::save nên theme lưu trong flash KHÔNG bị đổi.
// GIAI ĐOẠN 4 (Kind::QUICK_APP): vào app rồi thoát NGAY, thời gian ở lại lệch từng vòng
//   (kQuickMinMs .. kQuickMinMs+kQuickSpanMs) để bắt race khi task nền còn đang chạy.
// GIAI ĐOẠN 5 (job có audio = true): phát một bài MP3 từ /music rồi chạy đúng kịch bản của
//   job (APP / QUICK_APP / IDLE). Ngoài bộ nhớ còn đo DỨT TIẾNG: cứ mỗi kWinMs so tiến độ
//   AudioService::positionMs() với đồng hồ thật; tỉ lệ < kStallRatio là một "cửa sổ dứt".
//   Bài hết thì tự phát lại. Job audio_idle (Main đứng yên) là mẫu đối chứng để hiệu chỉnh
//   ngưỡng: nếu audio_ratio_min của nó đã thấp thì chỉnh kStallRatio, đừng tin các job khác.
//   Giới hạn: không có API đếm underrun, nên số đo này chỉ đúng nếu positionMs() bám theo
//   lượng PCM thật sự ra I2S. VẪN PHẢI NGHE bằng tai trong lúc chạy.
// GIAI ĐOẠN 6 (Kind::SD_PROBE + Prompt): rút/cắm thẻ SD. Chương trình in lời nhắc và đếm
//   ngược kPromptMs; bạn làm theo lời nhắc (có dòng ">>> RUT THE SD" / ">>> CAM THE SD LAI").
//   Kiểm tra: không treo, không rò rỉ khi app chạy lúc không có thẻ, và SdStorage có tự đọc
//   lại được sau khi cắm thẻ không (probe in sd_probe_in).
//
// Mẫu cuối warm-up là mốc tham chiếu. Kết luận mỗi job dựa trên: (mẫu cuối - mốc), đường
// xu hướng bình phương tối thiểu, số task, và việc AppManager có thật sự chuyển app không.
// Crash/reset giữa chừng: log sẽ in lại "MOC 0" từ đầu, hãy xem dòng cuối trước đó.
//
// Không đọc Serial ở đây vì InputService dùng Serial làm nguồn phím. Không bấm phím khi đang
// chạy. Muốn chạy lại: bấm RESET. Mọi dòng dữ liệu bắt đầu bằng "CSV" (bộ nhớ) hoặc "SD".
//
// Cờ build (thêm vào build_flags của env leak_test):
//   -DLEAK_SKIP_PHASE2=1   bỏ 5 job app của giai đoạn 2
//   -DLEAK_SKIP_PHASE34=1  bỏ giai đoạn 3 và 4 (theme, thoát nhanh)
//   -DLEAK_SKIP_PHASE5=1   bỏ giai đoạn 5 (nhạc phát nền)
//   -DLEAK_SKIP_PHASE6=1   bỏ giai đoạn 6 (rút/cắm thẻ SD, cần có người)
//   -DLEAK_PHASE5_SUSPECTS_ONLY=1  chỉ chạy 4 job nghi vấn của giai đoạn 5

#ifdef LEAK_PHASE5_SUSPECTS_ONLY
#ifndef LEAK_SKIP_PHASE2
#define LEAK_SKIP_PHASE2 1
#endif
#ifndef LEAK_SKIP_PHASE34
#define LEAK_SKIP_PHASE34 1
#endif
#ifndef LEAK_SKIP_PHASE6
#define LEAK_SKIP_PHASE6 1
#endif
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <string>
#include <vector>

#include "core/app_manager.hpp"
#include "core/lvgl_port.hpp"
#include "core/theme.hpp"
#include "services/audio_service.hpp"
#include "services/input_service.hpp"
#include "services/sd_storage.hpp"
#include "services/settings_store.hpp"
#include "services/time_service.hpp"

namespace {

// ---------- Cấu hình ----------
constexpr uint32_t kBootWaitMs = 10000;  // chờ sau khi vào Main Screen
constexpr uint32_t kSampleMs = 1000;     // chu kỳ mẫu của mốc 0 và của job IDLE
constexpr int kBaseSamples = 30;         // số mẫu của mốc 0
constexpr uint32_t kDwellMs = 1500;      // ở lại trong app (giai đoạn 2)
constexpr uint32_t kSettleMs = 2000;     // chờ ở Main trước khi lấy mẫu (giai đoạn 2)
constexpr uint32_t kSwitchTimeoutMs = 3000;  // quá hạn mà AppManager chưa chuyển = lỗi
constexpr uint32_t kReloadSettleMs = 1200;   // chờ sau mỗi lần đổi theme + reload
constexpr uint32_t kQuickMinMs = 50;         // thoát nhanh: thời gian ở lại tối thiểu
constexpr uint32_t kQuickSpanMs = 400;       // ... cộng thêm 0..span-1 ms, lệch từng vòng
constexpr uint32_t kQuickSettleMs = 3000;    // chờ task nền dọn xong sau khi thoát nhanh
constexpr int kMaxCycles = 300;

// Dứt tiếng (giai đoạn 5).
constexpr uint32_t kWinMs = 500;          // độ dài cửa sổ đo tiến độ phát
constexpr uint32_t kAudioPrimeMs = 1500;  // bỏ qua ngay sau khi phát (ring đang nạp)
constexpr double kStallRatio = 0.85;      // tiến độ / thời gian thật dưới mức này = dứt
constexpr int kMinWindows = 5;            // ít hơn thế thì coi như không đo được
constexpr size_t kTrackLen = 160;         // độ dài tối đa đường dẫn bài hát

// Thẻ SD (giai đoạn 6).
constexpr uint32_t kPromptMs = 20000;     // thời gian để rút/cắm thẻ
constexpr uint32_t kPromptTickMs = 5000;  // in nhắc mỗi chừng này
constexpr uint32_t kProbeSlowMs = 2000;   // một lần liệt kê thư mục lâu hơn thế = treo/chậm

// Dao động chấp nhận được (byte).
constexpr int32_t kTolPsram = 512;
constexpr int32_t kTolInternal = 512;

enum class Kind : uint8_t { APP, QUICK_APP, THEME_SETTINGS, THEME_MAIN, IDLE, SD_PROBE };
enum class Prompt : uint8_t { NONE, PULL_SD, INSERT_SD };

struct Job {
  Kind kind;
  AppId app;         // app được đo (THEME_MAIN/IDLE/SD_PROBE: MAIN_SCREEN)
  const char* name;
  int warm;          // số vòng khởi động nguội, không tính (>= 1)
  int cycles;        // số vòng đo (<= kMaxCycles)
  bool audio;        // true = phát nhạc nền suốt job và đo dứt tiếng
  Prompt prompt;     // lời nhắc người dùng trước khi bắt đầu phần đo
  // Hàm tạo constexpr (không dùng khởi tạo thành viên mặc định) để vẫn hợp lệ với C++11.
  constexpr Job(Kind k, AppId a, const char* n, int w, int c, bool au = false,
                Prompt p = Prompt::NONE)
      : kind(k), app(a), name(n), warm(w), cycles(c), audio(au), prompt(p) {}
};

// Chỉnh cycles ở đây. Muốn bỏ job nào: xóa dòng đó. Muốn đo thử nhanh: đặt cycles = 10.
constexpr Job kJobs[] = {
#ifndef LEAK_SKIP_PHASE2
    {Kind::APP, AppId::APP_PET, "pet", 3, 100},
    {Kind::APP, AppId::APP_ALBUM, "album", 3, 100},
    {Kind::APP, AppId::APP_MUSIC_PLAYER, "music", 3, 100},
    {Kind::APP, AppId::APP_VISUALIZER, "visualizer", 3, 300},
    {Kind::APP, AppId::APP_SETTINGS, "settings", 3, 100},
#endif
#ifndef LEAK_SKIP_PHASE34
    {Kind::THEME_SETTINGS, AppId::APP_SETTINGS, "theme_settings", 3, 100},
    {Kind::THEME_MAIN, AppId::MAIN_SCREEN, "theme_main", 3, 100},
    {Kind::QUICK_APP, AppId::APP_ALBUM, "quick_album", 3, 100},
    {Kind::QUICK_APP, AppId::APP_MUSIC_PLAYER, "quick_music", 3, 100},
#endif
#ifndef LEAK_SKIP_PHASE5
    {Kind::IDLE, AppId::MAIN_SCREEN, "audio_idle", 1, 30, true},
#ifndef LEAK_PHASE5_SUSPECTS_ONLY
    {Kind::APP, AppId::APP_PET, "audio_pet", 3, 60, true},
    {Kind::APP, AppId::APP_ALBUM, "audio_album", 3, 60, true},
#endif
    {Kind::APP, AppId::APP_VISUALIZER, "audio_visualizer", 3, 100, true},
#ifndef LEAK_PHASE5_SUSPECTS_ONLY
    {Kind::APP, AppId::APP_MUSIC_PLAYER, "audio_music", 3, 60, true},
#endif
    {Kind::QUICK_APP, AppId::APP_ALBUM, "audio_quick_album", 3, 60, true},
    {Kind::QUICK_APP, AppId::APP_MUSIC_PLAYER, "audio_quick_music", 3, 60, true},
#endif
#ifndef LEAK_SKIP_PHASE6
    {Kind::SD_PROBE, AppId::MAIN_SCREEN, "sd_probe_before", 1, 1},
    {Kind::APP, AppId::APP_VISUALIZER, "sd_pull_play_visualizer", 2, 20, true, Prompt::PULL_SD},
    {Kind::APP, AppId::APP_MUSIC_PLAYER, "sd_out_music", 2, 20},
    {Kind::APP, AppId::APP_ALBUM, "sd_out_album", 2, 20},
    {Kind::SD_PROBE, AppId::MAIN_SCREEN, "sd_probe_out", 1, 1},
    {Kind::SD_PROBE, AppId::MAIN_SCREEN, "sd_probe_in", 1, 1, false, Prompt::INSERT_SD},
    {Kind::APP, AppId::APP_ALBUM, "sd_back_album", 2, 20},
    {Kind::APP, AppId::APP_MUSIC_PLAYER, "sd_back_music", 2, 20},
#endif
};
constexpr int kNumJobs = sizeof(kJobs) / sizeof(kJobs[0]);

// Kiểu C++11 (một câu lệnh return, đệ quy): Arduino core ESP32 2.0.x mặc định gnu++11.
constexpr bool jobsValidFrom(int i) {
  return i >= kNumJobs ||
         (kJobs[i].warm >= 1 && kJobs[i].cycles >= 1 && kJobs[i].cycles <= kMaxCycles &&
          jobsValidFrom(i + 1));
}
static_assert(jobsValidFrom(0), "kJobs: warm >= 1, 1 <= cycles <= kMaxCycles");
static_assert(kBaseSamples >= 2, "can it nhat 2 mau");

// ---------- Dữ liệu mẫu ----------
struct Sample {
  uint32_t tMs;
  uint32_t heapFree;     // heap nội bộ (SRAM)
  uint32_t heapLargest;  // khối trống lớn nhất của heap nội bộ
  uint32_t psramFree;
  uint32_t psramLargest;
  uint32_t tasks;
  uint32_t loopStackMin;  // stack còn trống tối thiểu của task loop() (byte)
};

Sample take(uint32_t now) {
  Sample s;
  s.tMs = now;
  s.heapFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  s.heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  s.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  s.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  s.tasks = uxTaskGetNumberOfTasks();
  s.loopStackMin = uxTaskGetStackHighWaterMark(nullptr);
  return s;
}

// Hồi quy tuyến tính y = a + b*x trên x = 0..n-1, trả độ dốc b (byte/vòng).
double slopeOf(const uint32_t* y, int n) {
  if (n < 2) return 0.0;
  double sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (int i = 0; i < n; ++i) {
    sx += i;
    sy += y[i];
    sxy += (double)i * y[i];
    sxx += (double)i * i;
  }
  const double den = n * sxx - sx * sx;
  return den == 0.0 ? 0.0 : (n * sxy - sx * sy) / den;
}

// ---------- Mốc 0 ----------
Sample g_base[kBaseSamples];
int g_baseCount = 0;

void printBaseSummary() {
  uint32_t hMin = UINT32_MAX, hMax = 0, pMin = UINT32_MAX, pMax = 0;
  uint32_t tMin = UINT32_MAX, tMax = 0, sMin = UINT32_MAX;
  for (int i = 0; i < g_baseCount; ++i) {
    const Sample& s = g_base[i];
    if (s.heapFree < hMin) hMin = s.heapFree;
    if (s.heapFree > hMax) hMax = s.heapFree;
    if (s.psramFree < pMin) pMin = s.psramFree;
    if (s.psramFree > pMax) pMax = s.psramFree;
    if (s.tasks < tMin) tMin = s.tasks;
    if (s.tasks > tMax) tMax = s.tasks;
    if (s.loopStackMin < sMin) sMin = s.loopStackMin;
  }
  const Sample& last = g_base[g_baseCount - 1];
  const bool heapOk = (int32_t)(hMax - hMin) <= kTolInternal;
  const bool psramOk = (int32_t)(pMax - pMin) <= kTolPsram;
  const bool tasksOk = (tMin == tMax);
  Serial.println("=== MOC 0: TOM TAT ===");
  Serial.printf("heap noi bo: %lu..%lu B -> %s\n", (unsigned long)hMin, (unsigned long)hMax,
                heapOk ? "ON DINH" : "KHONG ON DINH");
  Serial.printf("PSRAM      : %lu..%lu B -> %s\n", (unsigned long)pMin, (unsigned long)pMax,
                psramOk ? "ON DINH" : "KHONG ON DINH");
  Serial.printf("So task    : %lu..%lu -> %s\n", (unsigned long)tMin, (unsigned long)tMax,
                tasksOk ? "ON DINH" : "THAY DOI");
  Serial.printf("Stack loop() con trong toi thieu: %lu B\n", (unsigned long)sMin);
  Serial.printf("BASE,heap_free=%lu,psram_free=%lu,tasks=%lu\n", (unsigned long)last.heapFree,
                (unsigned long)last.psramFree, (unsigned long)last.tasks);
  Serial.println("======================");
}

// ---------- Kết quả từng job ----------
struct Result {
  bool ran = false;
  bool switchOk = true;
  bool psramOk = true, heapOk = true, tasksOk = true;
  bool audioOk = true;   // phát được nhạc và đo được đủ cửa sổ
  bool stallOk = true;   // không có cửa sổ dứt tiếng
  bool probeOk = true;   // SD_PROBE: không treo và (sau khi cắm lại) đọc lại được
  const char* probeNote = "";
  int32_t psramDrift = 0, heapDrift = 0, largestDrift = 0;
  double psramSlope = 0, heapSlope = 0;
  int taskDelta = 0;
  uint32_t stackMin = 0;
  int aWin = 0, aStall = 0;
  double aRatio = -1.0;
  uint32_t aGapMax = 0;  // loop() bị chặn lâu nhất bao nhiêu ms trong job audio
};

Result g_results[kNumJobs];

// mẫu của job hiện tại: [0] = mốc (cuối warm-up), [1..cycles] = các vòng đo
uint32_t g_psram[kMaxCycles + 1];
uint32_t g_heap[kMaxCycles + 1];
uint32_t g_largest[kMaxCycles + 1];
Sample g_ref;
uint32_t g_stackMin;

enum class Phase : uint8_t { BOOT_WAIT, BASELINE, JOBS, DONE };
enum class Step : uint8_t {
  ENTER,             // APP/QUICK_APP: gọi goTo(app)
  DWELL,             // APP/QUICK_APP: chờ app hiện rồi ở lại
  SETTLE,            // APP/QUICK_APP: về Main, chờ rồi lấy mẫu
  THEME_GOTO,        // THEME_SETTINGS: gọi goTo(Settings)
  THEME_WAIT_ENTER,  // THEME_*: chờ app hiện và ổn định
  THEME_SELECT,      // THEME_*: select theme kế + reload
  THEME_SETTLE,      // THEME_*: chờ dựng lại màn hình
  THEME_LEAVE,       // THEME_SETTINGS: về Main sau khi xong
  IDLE_WAIT,         // IDLE: đứng yên ở Main, mỗi kSampleMs một mẫu
  PROBE,             // SD_PROBE: liệt kê thư mục và in kết quả
  PROMPT,            // nhắc người dùng rút/cắm thẻ rồi đếm ngược
  FAIL,              // không bắt đầu được job (vd không phát được nhạc)
};

Phase g_phase = Phase::BOOT_WAIT;
Step g_step = Step::ENTER;
Step g_afterPrompt = Step::ENTER;
uint32_t g_phaseStart = 0;
uint32_t g_stepStart = 0;
uint32_t g_lastSample = 0;
int g_job = 0;    // chỉ số job hiện tại
int g_cycle = 0;  // vòng hiện tại (0..warm+cycles-1)
int g_nSamples = 0;
bool g_jobFailed = false;
bool g_startFail = false;   // không phát được nhạc ở đầu job
bool g_arrived = false;     // app đã hiện (APP/QUICK_APP)
uint32_t g_dwellMs = 0;     // thời gian ở lại của vòng hiện tại
size_t g_themeStart = 0;    // chỉ số theme ban đầu của vòng
size_t g_themeIdx = 0;      // đã đổi bao nhiêu theme trong vòng
const char* g_origKey = nullptr;  // khóa theme ban đầu của job (để trả lại)
bool g_promptShown = false;
uint32_t g_promptTick = 0;

bool isThemeKind(Kind k) { return k == Kind::THEME_SETTINGS || k == Kind::THEME_MAIN; }

// ---------- Nhạc nền và đo dứt tiếng ----------
struct AudioMon {
  bool valid;         // cửa sổ hiện tại bắt đầu từ trạng thái PLAYING hợp lệ
  uint32_t winStart;
  uint32_t lastPos;
  int windows, stalls, skipped;
  double minRatio;
  int restarts, playFail;
  uint32_t gapMax, winGapMax, lastTick;  // khoảng cách giữa hai lần audioTick = loop bị chặn bao lâu
  int stallLogged;
};

AudioMon g_mon;
bool g_audioActive = false;
bool g_audioRestart = true;  // phát lại khi bài hết hoặc lỗi
uint32_t g_audioStart = 0;
uint32_t g_lastRestart = 0;
char g_track[kTrackLen];

void resetMon() {
  g_mon.valid = false;
  g_mon.winStart = 0;
  g_mon.lastPos = 0;
  g_mon.windows = 0;
  g_mon.stalls = 0;
  g_mon.skipped = 0;
  g_mon.minRatio = 9.9;
  g_mon.restarts = 0;
  g_mon.playFail = 0;
  g_mon.gapMax = 0;
  g_mon.winGapMax = 0;
  g_mon.lastTick = 0;
  g_mon.stallLogged = 0;
}

const char* audioStateName(AudioState s) {
  switch (s) {
    case AudioState::STOPPED: return "STOPPED";
    case AudioState::PLAYING: return "PLAYING";
    case AudioState::PAUSED: return "PAUSED";
    case AudioState::ERROR: return "ERROR";
  }
  return "?";
}

bool startAudio(uint32_t now) {
  if (!AudioService::begin()) return false;
  g_track[0] = '\0';
  {
    const std::vector<std::string> files = SdStorage::listFiles("/music", ".mp3");
    if (files.empty()) return false;
    strncpy(g_track, files[0].c_str(), kTrackLen - 1);
    g_track[kTrackLen - 1] = '\0';
  }
  if (!AudioService::play(g_track)) return false;
  g_audioActive = true;
  g_audioStart = now;
  g_lastRestart = now;
  Serial.printf("AUDIO,phat,%s\n", g_track);
  return true;
}

void stopAudio() {
  if (!g_audioActive) return;
  AudioService::stop();
  g_audioActive = false;
}

// Gọi mỗi vòng loop(). Không chặn.
void audioTick() {
  if (!g_audioActive) return;
  // Đọc đồng hồ NGAY ở đây, không dùng mốc đầu loop(): AppManager::tick() có thể đã chặn loop()
  // hàng chục ms (chuyển app giữ LvglLock), khiến mốc thời gian cũ và vị trí phát đọc lệch nhau
  // và tạo ra "dứt tiếng" giả ở cửa sổ kế tiếp.
  const uint32_t now = millis();
  if (g_mon.lastTick != 0) {
    const uint32_t gap = now - g_mon.lastTick;
    if (gap > g_mon.gapMax) g_mon.gapMax = gap;
    if (gap > g_mon.winGapMax) g_mon.winGapMax = gap;
  }
  g_mon.lastTick = now;
  const AudioState st = AudioService::state();
  if (st == AudioState::STOPPED || st == AudioState::ERROR) {
    g_mon.valid = false;
    if (g_audioRestart && now - g_lastRestart >= 300) {
      g_lastRestart = now;
      if (AudioService::play(g_track)) {
        ++g_mon.restarts;
        g_audioStart = now;
      } else if (++g_mon.playFail >= 5) {
        g_audioRestart = false;  // thẻ hỏng/rút: đừng gọi play() dồn dập
      }
    }
    return;
  }
  if (now - g_audioStart < kAudioPrimeMs) {
    g_mon.valid = false;
    return;
  }
  const uint32_t pos = AudioService::positionMs();
  if (!g_mon.valid) {
    g_mon.valid = (st == AudioState::PLAYING);
    g_mon.winStart = now;
    g_mon.lastPos = pos;
    g_mon.winGapMax = 0;
    return;
  }
  const uint32_t dt = now - g_mon.winStart;
  if (dt < kWinMs) return;
  if (st == AudioState::PLAYING && pos >= g_mon.lastPos) {
    const double ratio = (double)(pos - g_mon.lastPos) / (double)dt;
    ++g_mon.windows;
    if (ratio < g_mon.minRatio) g_mon.minRatio = ratio;
    if (ratio < kStallRatio) {
      ++g_mon.stalls;
      if (g_mon.stallLogged < 10) {  // chỉ in 10 lần đầu của mỗi job
        ++g_mon.stallLogged;
        Serial.printf("AUDIO,dut,%s,t_ms=%lu,ratio=%.2f,loop_gap_max_cua_so_ms=%lu,app=%d\n",
                      kJobs[g_job].name, (unsigned long)now, ratio,
                      (unsigned long)g_mon.winGapMax, (int)AppManager::current());
      }
    }
  } else {
    ++g_mon.skipped;  // tạm dừng, đổi bài hoặc tua: không so được
  }
  g_mon.winStart = now;
  g_mon.lastPos = pos;
  g_mon.winGapMax = 0;
  g_mon.valid = (st == AudioState::PLAYING);
}

// ---------- Thẻ SD ----------
struct ProbeResult {
  bool mounted;
  size_t images, music;
  bool slideshowTxt;
  uint32_t msImages, msMusic;
};

size_t g_sdImages0 = 0, g_sdMusic0 = 0;  // số file thấy lúc thẻ còn nguyên (sd_probe_before)
bool g_sdBaseSet = false;

ProbeResult probeSd() {
  ProbeResult p;
  p.mounted = SdStorage::isMounted();
  uint32_t t = millis();
  p.images = SdStorage::listFiles("/images", ".jpg;.jpeg").size();
  p.msImages = millis() - t;
  t = millis();
  p.music = SdStorage::listFiles("/music", ".mp3").size();
  p.msMusic = millis() - t;
  p.slideshowTxt = SdStorage::exists("/data/slideshow.txt");
  return p;
}

void printProbe(const char* name, const char* when, const ProbeResult& p) {
  Serial.printf("SD,%s,%s,mounted=%d,images=%lu,music=%lu,slideshow_txt=%d,ms_images=%lu,ms_music=%lu\n",
                name, when, p.mounted ? 1 : 0, (unsigned long)p.images, (unsigned long)p.music,
                p.slideshowTxt ? 1 : 0, (unsigned long)p.msImages, (unsigned long)p.msMusic);
}

bool probeSlow(const ProbeResult& p) { return p.msImages > kProbeSlowMs || p.msMusic > kProbeSlowMs; }

// ---------- Điều khiển job ----------
void printCsvHeader() {
  Serial.println("CSV,job,cycle,t_ms,heap_free,heap_largest,psram_free,psram_largest,tasks");
}

void printCsvLine(const char* name, int cycle, const Sample& s) {
  Serial.printf("CSV,%s,%d,%lu,%lu,%lu,%lu,%lu,%lu\n", name, cycle, (unsigned long)s.tMs,
                (unsigned long)s.heapFree, (unsigned long)s.heapLargest,
                (unsigned long)s.psramFree, (unsigned long)s.psramLargest,
                (unsigned long)s.tasks);
}

size_t indexOfCurrentTheme() {  // gọi khi đang giữ LvglLock
  const char* cur = Themes::current().key;
  for (size_t i = 0; i < Themes::count(); ++i) {
    if (strcmp(Themes::at(i).key, cur) == 0) return i;
  }
  return 0;
}

void restoreTheme() {  // trả về theme ban đầu của job, reload nếu đang khác
  if (g_origKey == nullptr) return;
  LvglLock lock;
  if (strcmp(Themes::current().key, g_origKey) != 0) {
    Themes::select(g_origKey);
    AppManager::reload();
  }
}

Step firstStepOf(Kind k) {
  switch (k) {
    case Kind::THEME_SETTINGS: return Step::THEME_GOTO;
    case Kind::THEME_MAIN: return Step::THEME_WAIT_ENTER;
    case Kind::IDLE: return Step::IDLE_WAIT;
    case Kind::SD_PROBE: return Step::PROBE;
    default: return Step::ENTER;
  }
}

void startJob(uint32_t now) {
  const Job& j = kJobs[g_job];
  g_cycle = 0;
  g_nSamples = 0;
  g_jobFailed = false;
  g_startFail = false;
  g_arrived = false;
  g_stackMin = UINT32_MAX;
  g_themeIdx = 0;
  g_origKey = nullptr;
  g_promptShown = false;
  g_stepStart = now;
  if (isThemeKind(j.kind)) {
    LvglLock lock;
    g_origKey = Themes::current().key;
  }
  g_afterPrompt = firstStepOf(j.kind);
  g_step = g_afterPrompt;
  Serial.printf("=== %s: %d vong khoi dong + %d vong do%s ===\n", j.name, j.warm, j.cycles,
                j.audio ? " (co nhac nen)" : "");
  if (j.audio) {
    resetMon();
    g_audioRestart = (j.prompt != Prompt::PULL_SD);  // sắp rút thẻ: đừng phát lại
    if (!startAudio(now)) {
      Serial.println("LOI: khong phat duoc nhac (can it nhat 1 file .mp3 trong /music)");
      g_startFail = true;
      g_step = Step::FAIL;
      return;
    }
  }
  if (j.prompt != Prompt::NONE) g_step = Step::PROMPT;
}

void finishJob() {
  const Job& j = kJobs[g_job];
  Result& r = g_results[g_job];
  r.ran = true;
  r.switchOk = !g_jobFailed;
  if (g_nSamples >= 2) {
    const int n = g_nSamples;  // gồm mốc
    r.psramDrift = (int32_t)g_psram[n - 1] - (int32_t)g_psram[0];
    r.heapDrift = (int32_t)g_heap[n - 1] - (int32_t)g_heap[0];
    r.largestDrift = (int32_t)g_largest[n - 1] - (int32_t)g_largest[0];
    r.psramSlope = slopeOf(g_psram, n);
    r.heapSlope = slopeOf(g_heap, n);
    const double psramFit = r.psramSlope * (n - 1);
    const double heapFit = r.heapSlope * (n - 1);
    // Rò rỉ = bộ nhớ trống GIẢM quá ngưỡng, theo mẫu cuối hoặc theo đường xu hướng.
    r.psramOk = !(r.psramDrift < -kTolPsram || psramFit < -kTolPsram);
    r.heapOk = !(r.heapDrift < -kTolInternal || heapFit < -kTolInternal);
  }
  // taskDelta / tasksOk đã được cập nhật theo từng mẫu trong endOfCycle()
  r.stackMin = (g_stackMin == UINT32_MAX) ? 0 : g_stackMin;  // 0 = chưa lấy được mẫu nào
  Serial.printf("--- %s: PSRAM troi %ld B (%.1f B/vong), heap troi %ld B (%.1f B/vong), "
                "largest PSRAM troi %ld B\n",
                j.name, (long)r.psramDrift, r.psramSlope, (long)r.heapDrift, r.heapSlope,
                (long)r.largestDrift);
  if (j.audio) {
    const bool pull = (j.prompt == Prompt::PULL_SD);  // rút thẻ: dứt tiếng là đương nhiên
    r.aWin = g_mon.windows;
    r.aStall = g_mon.stalls;
    r.aRatio = g_mon.windows > 0 ? g_mon.minRatio : -1.0;
    r.aGapMax = g_mon.gapMax;
    r.audioOk = !g_startFail && g_mon.playFail == 0 && (pull || g_mon.windows >= kMinWindows);
    r.stallOk = pull || g_mon.stalls == 0;
    Serial.printf("--- %s: audio cua_so=%d dut=%d ratio_min=%.2f loop_gap_max_ms=%lu bo_qua=%d "
                  "phat_lai=%d play_loi=%d trang_thai_cuoi=%s\n",
                  j.name, g_mon.windows, g_mon.stalls, r.aRatio, (unsigned long)r.aGapMax,
                  g_mon.skipped, g_mon.restarts, g_mon.playFail,
                  audioStateName(AudioService::state()));
    stopAudio();
  } else if (g_startFail) {
    r.audioOk = false;
  }
  if (isThemeKind(j.kind)) restoreTheme();
}

const char* verdict(const Result& r) {
  if (!r.switchOk) return "LOI CHUYEN APP";
  if (!r.probeOk) return r.probeNote;
  if (!r.audioOk) return "AUDIO LOI";
  if (!r.stallOk) return "DUT TIENG";
  if (!r.psramOk || !r.heapOk || !r.tasksOk) return "RO RI/LECH";
  return "DAT";
}

void printFinalTable() {
  Serial.println("=== TONG KET ===");
  Serial.println("job,ket_qua,psram_troi_B,psram_B_moi_vong,heap_troi_B,heap_B_moi_vong,task_lech,"
                 "stack_loop_min,audio_cua_so,audio_dut,audio_ratio_min,loop_gap_max_ms");
  for (int i = 0; i < kNumJobs; ++i) {
    const Result& r = g_results[i];
    if (!r.ran) {
      Serial.printf("%s,KHONG CHAY\n", kJobs[i].name);
      continue;
    }
    Serial.printf("%s,%s,%ld,%.1f,%ld,%.1f,%d,%lu,%d,%d,%.2f,%lu\n", kJobs[i].name, verdict(r),
                  (long)r.psramDrift, r.psramSlope, (long)r.heapDrift, r.heapSlope, r.taskDelta,
                  (unsigned long)r.stackMin, r.aWin, r.aStall, r.aRatio,
                  (unsigned long)r.aGapMax);
  }
  Serial.println("task_lech = so task mau cuoi - so task moc (moc la cuoi warm-up). 0 la dat.");
  Serial.println("Theme: kiem tra bang mat icon/mau nut/vien focus o ca 3 theme (khong tu do duoc).");
  Serial.println("Nhac: audio_ratio_min cua audio_idle la mau doi chung; NGHE bang tai xem co dut tieng khong.");
  Serial.println("=========================");
}

void nextJob(uint32_t now) {
  ++g_job;
  if (g_job >= kNumJobs) {
    printFinalTable();
    g_phase = Phase::DONE;
  } else {
    startJob(now);
  }
}

void abortJob(uint32_t now, const char* msg) {
  Serial.printf("LOI: %s (%s)\n", msg, kJobs[g_job].name);
  g_jobFailed = true;
  finishJob();  // theme job tự trả theme ban đầu, audio job tự dừng nhạc
  AppManager::goHome();
  nextJob(now);
}

// Kết thúc một vòng: lấy mẫu nếu là vòng mốc/vòng đo. Trả true khi job đã đủ vòng.
bool endOfCycle(uint32_t now) {
  const Job& j = kJobs[g_job];
  const bool isRef = (g_cycle == j.warm - 1);
  const bool measured = (g_cycle >= j.warm);
  if (isRef || measured) {
    const Sample s = take(now);
    if (s.loopStackMin < g_stackMin) g_stackMin = s.loopStackMin;
    if (isRef) g_ref = s;
    const int idx = g_nSamples++;
    g_psram[idx] = s.psramFree;
    g_heap[idx] = s.heapFree;
    g_largest[idx] = s.psramLargest;
    printCsvLine(j.name, g_cycle - j.warm + 1, s);  // vòng 0 = mốc
    g_results[g_job].taskDelta = (int)s.tasks - (int)g_ref.tasks;
    g_results[g_job].tasksOk = (s.tasks == g_ref.tasks);
  }
  ++g_cycle;
  return g_cycle >= j.warm + j.cycles;
}

// Gọi mỗi vòng loop(). Không chặn.
void runJob(uint32_t now) {
  const Job& j = kJobs[g_job];

  switch (g_step) {
    // ---- Lời nhắc rút/cắm thẻ ----
    case Step::PROMPT: {
      if (!g_promptShown) {
        g_promptShown = true;
        g_stepStart = now;
        g_promptTick = now;
        if (j.prompt == Prompt::PULL_SD) {
          Serial.printf(">>> RUT THE SD NGAY BAY GIO (co %lu giay)\n",
                        (unsigned long)(kPromptMs / 1000));
        } else {
          Serial.printf(">>> CAM THE SD LAI (co %lu giay)\n", (unsigned long)(kPromptMs / 1000));
        }
        break;
      }
      const uint32_t elapsed = now - g_stepStart;
      if (now - g_promptTick >= kPromptTickMs && elapsed < kPromptMs) {
        g_promptTick = now;
        Serial.printf("... con %lu giay\n", (unsigned long)((kPromptMs - elapsed) / 1000));
      }
      if (elapsed >= kPromptMs) {
        if (j.prompt == Prompt::PULL_SD && g_audioActive) {
          Serial.printf("AUDIO sau khi rut the: %s\n", audioStateName(AudioService::state()));
        }
        g_step = g_afterPrompt;
        g_stepStart = now;
      }
      break;
    }

    case Step::FAIL:
      finishJob();
      AppManager::goHome();
      nextJob(now);
      break;

    // ---- SD_PROBE ----
    case Step::PROBE: {
      Result& r = g_results[g_job];
      const ProbeResult p = probeSd();
      printProbe(j.name, "hien_tai", p);
      bool slow = probeSlow(p);
      r.probeOk = true;
      if (!g_sdBaseSet && j.prompt == Prompt::NONE) {  // probe đầu tiên = mốc lúc thẻ còn nguyên
        g_sdImages0 = p.images;
        g_sdMusic0 = p.music;
        g_sdBaseSet = true;
      }
      if (j.prompt == Prompt::INSERT_SD) {
        const bool autoOk = (p.images == g_sdImages0 && p.music == g_sdMusic0);
        const bool b = SdStorage::begin();
        const ProbeResult q = probeSd();
        Serial.printf("SD,%s,goi_SdStorage::begin()=%d\n", j.name, b ? 1 : 0);
        printProbe(j.name, "sau_begin", q);
        slow = slow || probeSlow(q);
        const bool backOk = (q.images == g_sdImages0 && q.music == g_sdMusic0);
        if (autoOk) {
          Serial.println("SD,ket_luan,tu doc lai duoc sau khi cam the");
        } else if (backOk) {
          Serial.println("SD,ket_luan,chi doc lai duoc sau SdStorage::begin(): app/service can goi lai begin() khi cam the");
        } else {
          Serial.println("SD,ket_luan,KHONG doc lai duoc sau khi cam the (can khoi dong lai hoac them API mount lai)");
          r.probeOk = false;
          r.probeNote = "KHONG MOUNT LAI";
        }
      }
      if (slow) {
        r.probeOk = false;
        r.probeNote = "TREO/CHAM";
      }
      finishJob();
      nextJob(now);
      break;
    }

    // ---- IDLE (đứng yên ở Main, thường có nhạc) ----
    case Step::IDLE_WAIT:
      if (now - g_stepStart < kSampleMs) break;
      g_stepStart = now;
      if (endOfCycle(now)) {
        finishJob();
        nextJob(now);
      }
      break;

    // ---- APP / QUICK_APP ----
    case Step::ENTER:
      AppManager::goTo(j.app);  // trì hoãn, thực hiện ở tick() kế tiếp
      g_arrived = false;
      g_dwellMs = (j.kind == Kind::QUICK_APP)
                      ? kQuickMinMs + (uint32_t)(((uint32_t)g_cycle * 37u) % kQuickSpanMs)
                      : kDwellMs;
      g_step = Step::DWELL;
      g_stepStart = now;
      break;

    case Step::DWELL:
      if (AppManager::current() != j.app) {
        if (now - g_stepStart > kSwitchTimeoutMs) abortJob(now, "AppManager khong chuyen app");
        break;
      }
      if (!g_arrived) {  // tính giờ ở lại từ lúc app thật sự hiện
        g_arrived = true;
        g_stepStart = now;
      }
      if (now - g_stepStart >= g_dwellMs) {
        AppManager::goHome();
        g_step = Step::SETTLE;
        g_stepStart = now;
      }
      break;

    case Step::SETTLE: {
      if (AppManager::current() != AppId::MAIN_SCREEN) {
        if (now - g_stepStart > kSwitchTimeoutMs) abortJob(now, "AppManager khong ve Main");
        break;
      }
      const uint32_t settle = (j.kind == Kind::QUICK_APP) ? kQuickSettleMs : kSettleMs;
      if (now - g_stepStart < settle) break;
      if (endOfCycle(now)) {
        finishJob();
        nextJob(now);
      } else {
        g_step = Step::ENTER;
        g_stepStart = now;
      }
      break;
    }

    // ---- THEME_SETTINGS / THEME_MAIN ----
    case Step::THEME_GOTO:
      AppManager::goTo(j.app);
      g_step = Step::THEME_WAIT_ENTER;
      g_stepStart = now;
      break;

    case Step::THEME_WAIT_ENTER:
      if (AppManager::current() != j.app) {
        if (now - g_stepStart > kSwitchTimeoutMs) abortJob(now, "AppManager khong chuyen app");
        break;
      }
      if (now - g_stepStart < kSettleMs) break;  // cho app ổn định
      {
        LvglLock lock;
        g_themeStart = indexOfCurrentTheme();
      }
      g_themeIdx = 0;
      g_step = Step::THEME_SELECT;
      break;

    case Step::THEME_SELECT: {
      {
        LvglLock lock;  // Themes chỉ gọi từ ngữ cảnh UI
        const size_t n = Themes::count();
        const size_t idx = (g_themeStart + 1 + g_themeIdx) % n;
        Themes::select(Themes::at(idx).key);
        AppManager::reload();  // trì hoãn, dựng lại ở tick() kế tiếp
      }
      g_step = Step::THEME_SETTLE;
      g_stepStart = now;
      break;
    }

    case Step::THEME_SETTLE: {
      if (now - g_stepStart < kReloadSettleMs) break;
      if (AppManager::current() != j.app) {
        abortJob(now, "reload lam doi app hien tai");
        break;
      }
      ++g_themeIdx;
      size_t n;
      {
        LvglLock lock;
        n = Themes::count();
      }
      if (g_themeIdx < n) {
        g_step = Step::THEME_SELECT;
        break;
      }
      g_themeIdx = 0;  // đã đi hết các theme, đang ở theme ban đầu
      if (endOfCycle(now)) {
        finishJob();
        if (j.kind == Kind::THEME_SETTINGS) {
          AppManager::goHome();
          g_step = Step::THEME_LEAVE;
          g_stepStart = now;
        } else {
          nextJob(now);
        }
      } else {
        g_step = Step::THEME_SELECT;
      }
      break;
    }

    case Step::THEME_LEAVE:
      if (AppManager::current() == AppId::MAIN_SCREEN ||
          now - g_stepStart > kSwitchTimeoutMs) {
        nextJob(now);
      }
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // Trình tự giống firmware chính (ARCHITECTURE.md mục 5, 6.4, 6.8).
  LvglPort::init();
  {
    LvglLock lock;  // task LVGL đã chạy, mọi thao tác LVGL phải giữ khóa
    InputService::init();
  }
  SdStorage::begin();
  TimeService::begin();
  SettingsStore::begin();
  {
    LvglLock lock;
    AppManager::init();  // vào MAIN_SCREEN
  }

  g_phaseStart = millis();
}

void loop() {
  const uint32_t now = millis();
  AppManager::tick(now);  // tự lấy LvglLock bên trong
  audioTick();

  switch (g_phase) {
    case Phase::BOOT_WAIT:
      if (now - g_phaseStart >= kBootWaitMs) {
        Serial.println("=== MOC 0: bat dau do (Main Screen, dung yen, KHONG bam phim) ===");
        Serial.println("CSV,idx,t_ms,heap_free,heap_largest,psram_free,psram_largest,tasks,loop_stack_min");
        g_phase = Phase::BASELINE;
        g_lastSample = now - kSampleMs;  // lấy mẫu đầu ngay
      }
      break;

    case Phase::BASELINE:
      if (now - g_lastSample >= kSampleMs) {
        g_lastSample = now;
        g_base[g_baseCount] = take(now);
        const Sample& s = g_base[g_baseCount];
        Serial.printf("CSV,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", g_baseCount, (unsigned long)s.tMs,
                      (unsigned long)s.heapFree, (unsigned long)s.heapLargest,
                      (unsigned long)s.psramFree, (unsigned long)s.psramLargest,
                      (unsigned long)s.tasks, (unsigned long)s.loopStackMin);
        ++g_baseCount;
        if (g_baseCount >= kBaseSamples) {
          printBaseSummary();
          printCsvHeader();
          g_job = 0;
          startJob(now);
          g_phase = Phase::JOBS;
        }
      }
      break;

    case Phase::JOBS:
      runJob(now);
      break;

    case Phase::DONE:
      break;
  }

  vTaskDelay(pdMS_TO_TICKS(5));
}