#include "apps/music/disc_cover.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "core/psram.hpp"
#include "services/image_service.hpp"
#include "services/sd_storage.hpp"

namespace {

constexpr const char* kPathFile = "/data/slideshow.txt";
constexpr uint32_t kTaskStack = 8 * 1024;
constexpr int kTaskPrio = 1;
constexpr int kTaskCore = 0;

// Trang thai (UI chi doi khi !sBusy; task chi ghi ket qua roi ha sBusy cuoi cung)
std::string sPath;                 // duong dan anh trong cache / dang xu ly
int sDiameter = 0;
uint32_t sCorner = 0;
uint16_t* sFrames[DiscCover::kFrames];
lv_img_dsc_t sDsc[DiscCover::kFrames];
volatile int sReady = 0;           // so khung hop le
volatile bool sBusy = false;
volatile bool sFailed = false;

// Du lieu cho task (chi mot job tai mot thoi diem)
std::string sJobPath;
int sJobD = 0;
uint32_t sJobCorner = 0;

inline void unpack565(uint16_t p, float& r, float& g, float& b) {
  r = (float)((p >> 11) & 0x1F);
  g = (float)((p >> 5) & 0x3F);
  b = (float)(p & 0x1F);
}

inline uint16_t pack565(float r, float g, float b) {
  int ri = std::min(31, std::max(0, (int)(r + 0.5f)));
  int gi = std::min(63, std::max(0, (int)(g + 0.5f)));
  int bi = std::min(31, std::max(0, (int)(b + 0.5f)));
  return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

inline uint16_t hexTo565(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xFF) >> 3 << 11) | (((hex >> 8) & 0xFF) >> 2 << 5) | ((hex & 0xFF) >> 3));
}

// Lay mau song tuyen tinh tai (sx, sy) (toa do diem anh, da kep vao [0, w-1] x [0, h-1]).
uint16_t bilinear565(const uint16_t* px, int w, int h, float sx, float sy) {
  sx = std::min(std::max(sx, 0.0f), (float)(w - 1));
  sy = std::min(std::max(sy, 0.0f), (float)(h - 1));
  const int x0 = (int)sx, y0 = (int)sy;
  const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
  const float fx = sx - (float)x0, fy = sy - (float)y0;

  float r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
  unpack565(px[y0 * w + x0], r00, g00, b00);
  unpack565(px[y0 * w + x1], r10, g10, b10);
  unpack565(px[y1 * w + x0], r01, g01, b01);
  unpack565(px[y1 * w + x1], r11, g11, b11);

  const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
  return pack565(r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11,
                 g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11,
                 b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11);
}

// Cat hinh vuong chinh giua cua `s` roi lay mau ra d x d (khong meo).
void coverCrop(const Image& s, uint16_t* dst, int d) {
  const int side = std::min(s.w, s.h);
  const float ox = (float)(s.w - side) * 0.5f;
  const float oy = (float)(s.h - side) * 0.5f;
  const float scale = (float)side / (float)d;
  for (int y = 0; y < d; y++) {
    const float sy = oy + ((float)y + 0.5f) * scale - 0.5f;
    for (int x = 0; x < d; x++) {
      const float sx = ox + ((float)x + 0.5f) * scale - 0.5f;
      dst[y * d + x] = bilinear565(s.pixels, s.w, s.h, sx, sy);
    }
  }
}

void freeFrames() {
  sReady = 0;
  for (int i = 0; i < DiscCover::kFrames; i++) {
    Psram::release(sFrames[i]);
    sFrames[i] = nullptr;
  }
  memset(sDsc, 0, sizeof(sDsc));
}

// Dung kFrames khung tu `base` (d x d, vuong): khung k xoay k*kStepDeg do theo chieu kim dong ho,
// pixel ngoai hinh tron noi tiep to mau `corner`. Tra false neu het bo nho (da don sach).
bool renderFrames(const uint16_t* base, int d, uint16_t corner) {
  for (int k = 0; k < DiscCover::kFrames; k++) {
    sFrames[k] = static_cast<uint16_t*>(Psram::alloc((size_t)d * d * sizeof(uint16_t)));
    if (!sFrames[k]) {
      freeFrames();
      return false;
    }
  }

  const float c = (float)d * 0.5f;
  const float r2 = c * c;
  for (int k = 0; k < DiscCover::kFrames; k++) {
    const float th = (float)(k * DiscCover::kStepDeg) * (float)M_PI / 180.0f;
    const float cs = cosf(th), sn = sinf(th);
    uint16_t* out = sFrames[k];
    for (int y = 0; y < d; y++) {
      const float dy = (float)y + 0.5f - c;
      for (int x = 0; x < d; x++) {
        const float dx = (float)x + 0.5f - c;
        if (dx * dx + dy * dy > r2) {
          out[y * d + x] = corner;
          continue;
        }
        // Nghich dao phep xoay: diem dich (dx,dy) lay tu diem nguon xoay nguoc k*kStepDeg do
        const float sx = c + dx * cs + dy * sn;
        const float sy = c - dx * sn + dy * cs;
        out[y * d + x] = bilinear565(base, d, d, sx - 0.5f, sy - 0.5f);
      }
    }
    memset(&sDsc[k], 0, sizeof(sDsc[k]));
    sDsc[k].header.cf = LV_IMG_CF_TRUE_COLOR;
    sDsc[k].header.w = d;
    sDsc[k].header.h = d;
    sDsc[k].data_size = (uint32_t)d * d * 2;
    sDsc[k].data = reinterpret_cast<const uint8_t*>(out);
    vTaskDelay(1);   // nhuong CPU (idle task, task khac) giua cac khung
  }
  sReady = DiscCover::kFrames;
  return true;
}

// Doc duong dan anh tu /data/slideshow.txt (rong neu chua chon).
std::string readPhotoPath() {
  size_t sz = 0;
  void* raw = SdStorage::readFileToPsram(kPathFile, &sz);
  std::string out;
  if (raw) {
    const char* c = static_cast<const char*>(raw);
    for (size_t i = 0; i < sz && c[i] != '\0' && c[i] != '\n' && c[i] != '\r'; i++) out.push_back(c[i]);
    Psram::release(raw);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.empty() || out[0] != '/') out.clear();
  return out;
}

void worker(void*) {
  Image img;
  const int d = sJobD;
  bool ok = ImageService::decode(sJobPath.c_str(), d * 2, d * 2, &img);
  uint16_t* base = nullptr;
  if (ok && img.pixels && img.w > 0 && img.h > 0) {
    base = static_cast<uint16_t*>(Psram::alloc((size_t)d * d * sizeof(uint16_t)));
    if (base) coverCrop(img, base, d);
  }
  ImageService::free(&img);

  bool built = false;
  if (base) built = renderFrames(base, d, hexTo565(sJobCorner));
  Psram::release(base);

  sFailed = !built;
  sBusy = false;   // cuoi cung: UI chi doc ket qua khi co nay ha xuong
  vTaskDelete(nullptr);
}

}  // namespace

namespace DiscCover {

void request(int diameter, uint32_t cornerColor) {
  if (sBusy || diameter <= 0) return;

  const std::string path = readPhotoPath();
  if (path.empty()) {
    freeFrames();
    sPath.clear();
    sFailed = false;
    return;
  }
  // Co cache dung anh + kich thuoc + mau goc: dung lai. Loi truoc do thi thu lai o lan vao nay.
  if (sReady == kFrames && path == sPath && diameter == sDiameter && cornerColor == sCorner) return;

  freeFrames();
  sPath = path;
  sDiameter = diameter;
  sCorner = cornerColor;
  sFailed = false;
  sJobPath = path;
  sJobD = diameter;
  sJobCorner = cornerColor;
  sBusy = true;
  if (xTaskCreatePinnedToCore(worker, "disc_cover", kTaskStack, nullptr, kTaskPrio, nullptr, kTaskCore) !=
      pdPASS) {
    sBusy = false;
    sFailed = true;
  }
}

bool settled() { return !sBusy; }

int frameCount() { return sBusy ? 0 : (int)sReady; }

const lv_img_dsc_t* frame(int index) {
  const int n = frameCount();
  if (n <= 0) return nullptr;
  return &sDsc[((index % n) + n) % n];
}

}  // namespace DiscCover