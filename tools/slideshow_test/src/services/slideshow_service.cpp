// Anh dai dien Main Screen. Thiet ke cho nhe nhat:
// - Khong co task thuong truc, khong chu ky. Giai ma dung 1 lan khi can, task tu xoa.
// - Ket qua cache trong PSRAM (~71 KB voi khung 154x232), vao/ra Main khong giai ma lai.
// - Moi trang thai chung duoc bao ve boi gMutex. Khong bao gio giu gMutex trong luc giai ma,
//   nen thu tu khoa luon la gMutex -> khoa SD (chi o doc/ghi file nho), khong co chieu nguoc lai.

#include "services/slideshow_service.hpp"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "services/image_service.hpp"
#include "services/sd_storage.hpp"
#include "core/psram.hpp"

namespace {

constexpr const char* kPathFile = "/data/slideshow.txt";
constexpr size_t kMaxPathLen = 255;
constexpr uint32_t kTaskStack = 8192;
constexpr UBaseType_t kTaskPrio = 1;  // thap hon task LVGL (2)
constexpr BaseType_t kTaskCore = 0;

enum class State : uint8_t {
  UNLOADED,  // chua doc duong dan tu SD
  NO_PHOTO,  // khong co anh duoc chon
  IDLE,      // co duong dan, chua giai ma
  LOADING,   // task Core 0 dang giai ma
  READY,     // gImage/gDsc hop le
  FAILED     // giai ma loi (request() ke tiep se thu lai)
};

struct Job {
  std::string path;
  uint32_t gen;
  int w, h;
};

std::mutex gMutex;
State gState = State::UNLOADED;
std::string gPath;
uint32_t gGen = 0;  // tang moi khi doi/xoa anh, de bo ket qua cua job cu
Image gImage;
lv_img_dsc_t gDsc;
int gW = 0, gH = 0;

// Goi khi dang giu gMutex.
void dropCacheLocked() {
  ImageService::free(&gImage);
  gDsc = lv_img_dsc_t();
}

// Goi khi dang giu gMutex.
void loadPathLocked() {
  if (gState != State::UNLOADED) return;
  gPath.clear();
  size_t size = 0;
  void* buf = SdStorage::readFileToPsram(kPathFile, &size);
  if (buf) {
    const char* s = static_cast<const char*>(buf);
    size_t n = size > kMaxPathLen ? kMaxPathLen : size;
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) --n;
    gPath.assign(s, n);
    Psram::release(buf);
  }
  gState = gPath.empty() ? State::NO_PHOTO : State::IDLE;
}

// ---- Phu kin khung ("cover"): phong/thu anh cho phu het w x h roi cat chinh giua ----
// Khong phu thuoc decode() thu nho the nao: tu tinh he so, lay vung giua cua anh nguon va
// lay mau song tuyen tinh (RGB565) ra dung w x h. Phong to duoc neu anh nho hon khung.

inline uint16_t bilinear565(uint16_t p00, uint16_t p10, uint16_t p01, uint16_t p11, uint32_t fx,
                            uint32_t fy) {  // fx, fy: 0..256
  const uint32_t w00 = (256 - fx) * (256 - fy);
  const uint32_t w10 = fx * (256 - fy);
  const uint32_t w01 = (256 - fx) * fy;
  const uint32_t w11 = fx * fy;
  const uint32_t r = (((p00 >> 11) & 31) * w00 + ((p10 >> 11) & 31) * w10 +
                      ((p01 >> 11) & 31) * w01 + ((p11 >> 11) & 31) * w11) >> 16;
  const uint32_t g = (((p00 >> 5) & 63) * w00 + ((p10 >> 5) & 63) * w10 +
                      ((p01 >> 5) & 63) * w01 + ((p11 >> 5) & 63) * w11) >> 16;
  const uint32_t b = ((p00 & 31) * w00 + (p10 & 31) * w10 + (p01 & 31) * w01 +
                      (p11 & 31) * w11) >> 16;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Tao *dst co kich thuoc dung w x h (PSRAM). Tra false (dst rong) neu het bo nho.
bool coverResample(const Image& src, int w, int h, Image* dst) {
  if (!src.pixels || src.w <= 0 || src.h <= 0) return false;
  auto* out = static_cast<uint16_t*>(Psram::alloc(static_cast<size_t>(w) * h * 2));
  if (!out) return false;

  const float sx = static_cast<float>(w) / src.w;
  const float sy = static_cast<float>(h) / src.h;
  const float s = sx > sy ? sx : sy;  // he so phong/thu: lay canh can phu nhat
  const float x0 = (src.w - w / s) * 0.5f;  // goc vung nguon duoc lay (can giua)
  const float y0 = (src.h - h / s) * 0.5f;
  const float maxX = static_cast<float>(src.w - 1);
  const float maxY = static_cast<float>(src.h - 1);

  for (int dy = 0; dy < h; ++dy) {
    float fy = y0 + (dy + 0.5f) / s - 0.5f;
    if (fy < 0) fy = 0;
    if (fy > maxY) fy = maxY;
    const int iy = static_cast<int>(fy);
    const int iy1 = iy + 1 < src.h ? iy + 1 : iy;
    const uint32_t wy = static_cast<uint32_t>((fy - iy) * 256.0f);
    const uint16_t* row0 = src.pixels + static_cast<size_t>(iy) * src.w;
    const uint16_t* row1 = src.pixels + static_cast<size_t>(iy1) * src.w;
    uint16_t* drow = out + static_cast<size_t>(dy) * w;
    for (int dx = 0; dx < w; ++dx) {
      float fx = x0 + (dx + 0.5f) / s - 0.5f;
      if (fx < 0) fx = 0;
      if (fx > maxX) fx = maxX;
      const int ix = static_cast<int>(fx);
      const int ix1 = ix + 1 < src.w ? ix + 1 : ix;
      const uint32_t wx = static_cast<uint32_t>((fx - ix) * 256.0f);
      drow[dx] = bilinear565(row0[ix], row0[ix1], row1[ix], row1[ix1], wx, wy);
    }
  }
  dst->pixels = out;
  dst->w = w;
  dst->h = h;
  return true;
}

// Giai ma o kich thuoc gan nguyen ban (toi da 4x khung, decode khong phong to), roi phu kin khung.
constexpr int kDecodeScale = 4;

bool decodeCover(const char* path, int w, int h, Image* out) {
  Image src;
  if (!ImageService::decode(path, w * kDecodeScale, h * kDecodeScale, &src)) return false;
  Image dst;
  if (coverResample(src, w, h, &dst)) {
    ImageService::free(&src);
    *out = dst;
  } else {
    *out = src;  // het bo nho: dung anh goc, LVGL van cat theo khung (co the khong phu kin)
  }
  return true;
}

void runJob(Job* raw) {
  std::unique_ptr<Job> job(raw);
  Image img;
  const bool ok = decodeCover(job->path.c_str(), job->w, job->h, &img);

  std::lock_guard<std::mutex> lk(gMutex);
  if (job->gen != gGen) {  // anh da bi doi/xoa trong luc giai ma
    if (ok) ImageService::free(&img);
    return;
  }
  if (!ok) {
    gState = State::FAILED;
    Serial.printf("[Slideshow] giai ma loi: %s\n", job->path.c_str());
    return;
  }
  dropCacheLocked();
  gImage = img;
  gDsc = lv_img_dsc_t();
  gDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  gDsc.header.w = static_cast<uint32_t>(img.w);
  gDsc.header.h = static_cast<uint32_t>(img.h);
  gDsc.data_size = static_cast<uint32_t>(img.w) * static_cast<uint32_t>(img.h) * 2u;
  gDsc.data = reinterpret_cast<const uint8_t*>(img.pixels);
  gState = State::READY;
  Serial.printf("[Slideshow] san sang %dx%d\n", img.w, img.h);
}

void taskMain(void* arg) {
  runJob(static_cast<Job*>(arg));  // moi doi tuong da giai phong trong runJob
  vTaskDelete(nullptr);
}

// Goi khi dang giu gMutex.
void startJobLocked(int w, int h) {
  gW = w;
  gH = h;
  gState = State::LOADING;
  Job* job = new Job{gPath, gGen, w, h};
  if (xTaskCreatePinnedToCore(taskMain, "slideshow", kTaskStack, job, kTaskPrio, nullptr,
                              kTaskCore) != pdPASS) {
    delete job;
    gState = State::FAILED;
  }
}

}  // namespace

namespace SlideshowService {

bool setPhoto(const char* path) {
  if (!path || path[0] != '/') return false;
  const size_t len = strlen(path);
  if (len == 0 || len > kMaxPathLen) return false;

  std::lock_guard<std::mutex> lk(gMutex);
  ++gGen;
  dropCacheLocked();
  gPath.assign(path, len);
  gState = State::IDLE;
  const bool saved = SdStorage::writeFile(kPathFile, path, len);
  if (!saved) Serial.println("[Slideshow] khong ghi duoc /data/slideshow.txt");
  return saved;
}

void clearPhoto() {
  std::lock_guard<std::mutex> lk(gMutex);
  ++gGen;
  dropCacheLocked();
  gPath.clear();
  gState = State::NO_PHOTO;
  SdStorage::writeFile(kPathFile, "", 0);  // file rong = khong co anh
}

void request(int targetW, int targetH) {
  std::lock_guard<std::mutex> lk(gMutex);
  loadPathLocked();
  switch (gState) {
    case State::READY:
      if (gW == targetW && gH == targetH) return;  // cache con dung
      ++gGen;
      dropCacheLocked();
      startJobLocked(targetW, targetH);
      return;
    case State::IDLE:
    case State::FAILED:
      startJobLocked(targetW, targetH);
      return;
    default:  // NO_PHOTO, LOADING: khong lam gi
      return;
  }
}

bool settled() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gState != State::UNLOADED && gState != State::IDLE && gState != State::LOADING;
}

const lv_img_dsc_t* image() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gState == State::READY ? &gDsc : nullptr;
}

}  // namespace SlideshowService
