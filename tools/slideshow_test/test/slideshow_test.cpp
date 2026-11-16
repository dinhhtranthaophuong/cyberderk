// Test tren may tinh cho services/slideshow_service (white-box: include thang .cpp that).
// image_service.cpp va image_resize.cpp that duoc bien dich rieng va dung chung; chi
// TJpg_Decoder, SD, PSRAM, FreeRTOS, Arduino, LVGL la gia lap (xem stubs/).
#include <Arduino.h>
#include <TJpg_Decoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/psram.hpp"
#include "services/image_resize.hpp"
#include "services/image_service.hpp"
#include "services/sd_storage.hpp"

// ---------------------------------------------------------------- khung test
static int g_fail = 0, g_pass = 0, g_notes = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; printf("  [HONG] %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKM(c, ...) do { if (c) ++g_pass; else { ++g_fail; printf("  [HONG] %s:%d  %s | ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NOTE(...) do { ++g_notes; printf("  [GHI CHU] "); printf(__VA_ARGS__); printf("\n"); } while (0)

SerialStub Serial;
std::atomic<int> g_delayCalls{0};

// ---------------------------------------------------------------- PSRAM gia lap
static std::mutex g_pm;
static std::unordered_map<void*, size_t> g_blocks;
static size_t g_outBytes = 0, g_peakBytes = 0, g_psramLimit = 64u << 20;
static std::atomic<int> g_allocCalls{0}, g_allocFailAt{0} /*0 = khong*/;
static std::atomic<bool> g_allocFailAll{false};
static std::atomic<int> g_badFrees{0};
namespace Psram {
void* alloc(size_t n) {
  std::lock_guard<std::mutex> l(g_pm);
  ++g_allocCalls;
  if (g_allocFailAll) return nullptr;
  if (g_allocFailAt > 0 && g_allocCalls == g_allocFailAt) return nullptr;
  if (g_outBytes + n > g_psramLimit || n == 0) return nullptr;
  void* p = malloc(n);
  if (!p) return nullptr;
  memset(p, 0xCD, n);  // rac: doc truoc khi ghi se lo ra
  g_blocks[p] = n;
  g_outBytes += n;
  if (g_outBytes > g_peakBytes) g_peakBytes = g_outBytes;
  return p;
}
void release(void* p) {
  if (!p) return;
  std::lock_guard<std::mutex> l(g_pm);
  auto it = g_blocks.find(p);
  if (it == g_blocks.end()) { ++g_badFrees; return; }
  g_outBytes -= it->second;
  g_blocks.erase(it);
  free(p);
}
size_t freeBytes() { return g_psramLimit - g_outBytes; }
}  // namespace Psram
static size_t outBytes() { std::lock_guard<std::mutex> l(g_pm); return g_outBytes; }
static size_t outCount() { std::lock_guard<std::mutex> l(g_pm); return g_blocks.size(); }

// ---------------------------------------------------------------- SD gia lap
static std::mutex g_fsm;
static std::map<std::string, std::vector<uint8_t>> g_fs;
static std::atomic<bool> g_sdReadFail{false}, g_sdWriteFail{false};
static std::atomic<int> g_pathReads{0}, g_imgReads{0}, g_writes{0};
static std::function<void(const char*)> g_readHook;  // goi truoc khi doc file anh
static std::atomic<bool> g_strict{false};
static std::atomic<int> g_lockViolations{0};
static const char* kSlidePath = "/data/slideshow.txt";
static bool slideMutexHeld();  // dinh nghia sau khi include .cpp

namespace SdStorage {
void* readFileToPsram(const char* path, size_t* outSize) {
  if (outSize) *outSize = 0;
  const bool isPathFile = strcmp(path, kSlidePath) == 0;
  if (isPathFile) ++g_pathReads;
  else {
    ++g_imgReads;
    if (g_strict && slideMutexHeld()) ++g_lockViolations;  // decode KHONG duoc giu gMutex
    if (g_readHook) g_readHook(path);
  }
  std::vector<uint8_t> data;
  {
    std::lock_guard<std::mutex> l(g_fsm);
    if (g_sdReadFail) return nullptr;
    auto it = g_fs.find(path);
    if (it == g_fs.end() || it->second.empty()) return nullptr;
    data = it->second;
  }
  void* p = Psram::alloc(data.size());
  if (!p) return nullptr;
  memcpy(p, data.data(), data.size());
  if (outSize) *outSize = data.size();
  return p;
}
bool writeFile(const char* path, const void* data, size_t len) {
  ++g_writes;
  std::lock_guard<std::mutex> l(g_fsm);
  if (g_sdWriteFail) return false;
  const uint8_t* b = static_cast<const uint8_t*>(data);
  g_fs[path] = std::vector<uint8_t>(b, b + len);
  return true;
}
}  // namespace SdStorage
static std::string fsText(const char* p) {
  std::lock_guard<std::mutex> l(g_fsm);
  auto it = g_fs.find(p);
  return it == g_fs.end() ? std::string("<khong co>") : std::string(it->second.begin(), it->second.end());
}
static void fsPut(const std::string& p, const std::string& s) {
  std::lock_guard<std::mutex> l(g_fsm);
  g_fs[p] = std::vector<uint8_t>(s.begin(), s.end());
}

// ---------------------------------------------------------------- FreeRTOS gia lap
struct TaskDeleted {};
static std::mutex g_tm;
static std::vector<std::thread> g_threads;
static std::atomic<int> g_liveTasks{0}, g_tasksCreated{0}, g_returnedNoDelete{0};
static std::atomic<bool> g_taskCreateFail{false};
static uint32_t g_lastStack = 0; static UBaseType_t g_lastPrio = 0; static BaseType_t g_lastCore = -1;
static std::string g_lastName;
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t*, BaseType_t core) {
  if (g_taskCreateFail) return pdFAIL;
  g_lastStack = stack; g_lastPrio = prio; g_lastCore = core; g_lastName = name;
  ++g_tasksCreated; ++g_liveTasks;
  std::lock_guard<std::mutex> l(g_tm);
  g_threads.emplace_back([fn, arg] {
    try { fn(arg); ++g_returnedNoDelete; } catch (const TaskDeleted&) {}
    --g_liveTasks;
  });
  return pdPASS;
}
void vTaskDelete(TaskHandle_t h) { if (h == nullptr) throw TaskDeleted(); }
static bool waitTasks(int ms = 5000) {
  auto t0 = std::chrono::steady_clock::now();
  while (g_liveTasks > 0) {
    if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::lock_guard<std::mutex> l(g_tm);
  for (auto& t : g_threads) t.join();
  g_threads.clear();
  return true;
}
struct Sem { std::mutex m; std::condition_variable cv; bool taken = false; };
SemaphoreHandle_t xSemaphoreCreateMutex() { return new Sem; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, uint32_t) {
  Sem* s = static_cast<Sem*>(h); std::unique_lock<std::mutex> l(s->m);
  s->cv.wait(l, [&] { return !s->taken; }); s->taken = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  Sem* s = static_cast<Sem*>(h); { std::lock_guard<std::mutex> l(s->m); s->taken = false; }
  s->cv.notify_one(); return pdTRUE;
}

// ---------------------------------------------------------------- TJpgDec gia lap
// JPG gia: "FJPG" u16 w u16 h u32 seed u8 mode
//   mode 0 binh thuong | 1 loi giua chung (ghi do 1 nua roi tra loi) | 2 pixel phu (khong dung)
uint16_t fakeJpgPixel(uint32_t seed, int x, int y, int W, int H) {
  const uint32_t r = W > 1 ? (uint32_t)x * 31u / (uint32_t)(W - 1) : 0;
  const uint32_t g = H > 1 ? (uint32_t)y * 63u / (uint32_t)(H - 1) : 0;
  return (uint16_t)((r << 11) | (g << 5) | (seed & 31u));
}
TJpgDecoder TJpgDec;
static std::atomic<int> g_drawing{0}, g_maxDrawing{0};
static std::function<void()> g_drawHook;
JRESULT TJpgDecoder::getJpgSize(uint16_t* w, uint16_t* h, const uint8_t* d, uint32_t n) {
  if (n < 13 || memcmp(d, "FJPG", 4) != 0) return JDR_FMT1;
  *w = d[4] | (d[5] << 8); *h = d[6] | (d[7] << 8);
  return JDR_OK;
}
JRESULT TJpgDecoder::drawJpg(int32_t, int32_t, const uint8_t* d, uint32_t n) {
  if (n < 13 || memcmp(d, "FJPG", 4) != 0) return JDR_FMT1;
  int cur = ++g_drawing; int mx = g_maxDrawing; while (cur > mx && !g_maxDrawing.compare_exchange_weak(mx, cur)) {}
  if (g_drawHook) g_drawHook();
  const int W = d[4] | (d[5] << 8), H = d[6] | (d[7] << 8);
  const uint32_t seed = d[8] | (d[9] << 8) | (d[10] << 16) | ((uint32_t)d[11] << 24);
  const int mode = d[12], s = scale_ ? scale_ : 1;
  const int dw = std::max(1, W / s), dh = std::max(1, H / s);
  JRESULT res = JDR_OK;
  const int bx_n = (dw + 15) / 16, by_n = (dh + 15) / 16;
  uint16_t blk[16 * 16];
  for (int by = 0; by < by_n && res == JDR_OK; ++by)
    for (int bx = 0; bx < bx_n; ++bx) {
      if (mode == 1 && by * 2 >= by_n) { res = JDR_INP; break; }
      for (int j = 0; j < 16; ++j)
        for (int i = 0; i < 16; ++i) {
          const int sx = (bx * 16 + i) * s, sy = (by * 16 + j) * s;
          blk[j * 16 + i] = (sx < W && sy < H) ? fakeJpgPixel(seed, sx, sy, W, H) : 0xFFFF;
        }
      if (!cb_(bx * 16, by * 16, 16, 16, blk)) { res = JDR_INP; break; }
    }
  --g_drawing;
  return res;
}

// ---------------------------------------------------------------- code that can test
// gMutex duoc thay bang TrackedMutex de biet LUONG HIEN TAI co dang giu no hay khong
// (de kiem tra "khong bao gio giai ma / doc anh khi dang giu gMutex").
#include <memory>
#include <string>
#include <cstring>
#include <mutex>
class TrackedMutex {
 public:
  void lock() { m_.lock(); ++held(); }
  bool try_lock() { if (!m_.try_lock()) return false; ++held(); return true; }
  void unlock() { --held(); m_.unlock(); }
  static int& held() { static thread_local int h = 0; return h; }
 private:
  std::mutex m_;
};
namespace std { using tracked_mutex = ::TrackedMutex; }
#define mutex tracked_mutex
#include "services/slideshow_service.cpp"
#undef mutex
typedef std::lock_guard<std::tracked_mutex> GLock;
static bool slideMutexHeld() { return TrackedMutex::held() > 0; }

// ---------------------------------------------------------------- tien ich test
struct Photo { const char* path; int W, H, seed; };
static const Photo kPhotos[] = {
    {"/images/a_landscape.jpg", 4000, 3000, 3},
    {"/images/b_portrait.jpg", 3000, 4000, 5},
    {"/images/c_hd.jpg", 1280, 720, 7},
    {"/images/d_square.jpg", 800, 800, 9},
    {"/images/e_small.jpg", 100, 80, 11},    // nho hon khung: phai phong to
    {"/images/f_tiny.jpg", 7, 5, 13},
    {"/images/g_pano.jpg", 6000, 1000, 15},
    {"/images/h_wide1.jpg", 1000, 1, 17},
};
static const int kNPhotos = sizeof(kPhotos) / sizeof(kPhotos[0]);

static void putJpg(const char* path, int W, int H, int seed, int mode = 0) {
  std::vector<uint8_t> b = {'F', 'J', 'P', 'G', (uint8_t)W, (uint8_t)(W >> 8), (uint8_t)H, (uint8_t)(H >> 8),
                            (uint8_t)seed, 0, 0, 0, (uint8_t)mode, 1, 2, 3};
  std::lock_guard<std::mutex> l(g_fsm);
  g_fs[path] = b;
}
static void seedFs() {
  { std::lock_guard<std::mutex> l(g_fsm); g_fs.clear(); }
  for (auto& p : kPhotos) putJpg(p.path, p.W, p.H, p.seed);
}

// Dua service ve trang thai "vua khoi dong" (khong dung den SD).
static void resetService() {
  waitTasks();
  GLock lk(gMutex);
  dropCacheLocked();
  gState = State::UNLOADED; gPath.clear(); gW = gH = 0; ++gGen;
}
static void resetAll() {
  g_sdReadFail = g_sdWriteFail = false; g_allocFailAll = false; g_allocFailAt = 0; g_taskCreateFail = false;
  g_readHook = nullptr; g_drawHook = nullptr;
  resetService();
  seedFs();
  g_pathReads = g_imgReads = g_writes = 0; g_tasksCreated = 0; g_returnedNoDelete = 0;
  g_lockViolations = 0; g_maxDrawing = 0; g_allocCalls = 0;
}
static bool waitSettled(int ms = 5000) {
  auto t0 = std::chrono::steady_clock::now();
  while (!SlideshowService::settled()) {
    if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}
static State curState() { GLock l(gMutex); return gState; }
static uint32_t curGen() { GLock l(gMutex); return gGen; }
// Khoa doc/ghi don gian (C++11) mo phong hop dong: Album chi doi anh khi Main khong doc anh.
struct RwLock {
  std::mutex m; std::condition_variable cv; int readers = 0; bool writer = false;
  void lockShared() { std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return !writer; }); ++readers; }
  void unlockShared() { { std::lock_guard<std::mutex> l(m); --readers; } cv.notify_all(); }
  void lock() { std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return !writer && readers == 0; }); writer = true; }
  void unlock() { { std::lock_guard<std::mutex> l(m); writer = false; } cv.notify_all(); }
};
struct SharedGuard { RwLock& r; explicit SharedGuard(RwLock& x) : r(x) { r.lockShared(); } ~SharedGuard() { r.unlockShared(); } };
struct ExclGuard { RwLock& r; explicit ExclGuard(RwLock& x) : r(x) { r.lock(); } ~ExclGuard() { r.unlock(); } };

// Cong tru cua cong (gate) de giu 1 lan doc anh o giua chung.
struct Gate {
  std::mutex m; std::condition_variable cv; bool open = true; int waiting = 0;
  void close() { std::lock_guard<std::mutex> l(m); open = false; }
  void release() { { std::lock_guard<std::mutex> l(m); open = true; } cv.notify_all(); }
  void pass() { std::unique_lock<std::mutex> l(m); if (open) return; ++waiting; cv.wait(l, [&] { return open; }); --waiting; }
  bool waitBlocked(int n, int ms = 3000) {
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
      { std::lock_guard<std::mutex> l(m); if (waiting >= n) return true; }
      if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

// Kiem tra mot anh READY: kich thuoc, header, mau (do doc / giua / goc) khop hinh hoc "cover".
static void checkCover(const lv_img_dsc_t* d, int w, int h, const Photo& p, const char* tag, int tolR = 2, int tolG = 3) {
  CHECKM(d != nullptr, "%s: image() null", tag);
  if (!d) return;
  CHECKM(d->header.cf == LV_IMG_CF_TRUE_COLOR, "%s cf", tag);
  CHECKM(d->data && d->data_size == (uint32_t)w * (uint32_t)h * 2u, "%s data_size=%u mong %u", tag, d->data_size, w * h * 2);
  if (w <= 2047) CHECKM((int)d->header.w == w, "%s header.w=%u mong %d", tag, (unsigned)d->header.w, w);
  if (h <= 2047) CHECKM((int)d->header.h == h, "%s header.h=%u mong %d", tag, (unsigned)d->header.h, h);
  if (!d->data || d->data_size != (uint32_t)w * h * 2u) return;
  const uint16_t* px = reinterpret_cast<const uint16_t*>(d->data);
  const double S = std::max((double)w / p.W, (double)h / p.H);
  const double X0 = (p.W - w / S) / 2, Y0 = (p.H - h / S) / 2;
  int badB = 0, badR = 0, badG = 0;
  int worstR = 0, worstG = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint16_t v = px[(size_t)y * w + x];
      if ((int)(v & 31) != (p.seed & 31)) ++badB;
      double fx = X0 + (x + 0.5) / S - 0.5, fy = Y0 + (y + 0.5) / S - 0.5;
      fx = std::min(std::max(fx, 0.0), (double)p.W - 1); fy = std::min(std::max(fy, 0.0), (double)p.H - 1);
      const double eR = p.W > 1 ? 31.0 * fx / (p.W - 1) : 0, eG = p.H > 1 ? 63.0 * fy / (p.H - 1) : 0;
      const int dR = (int)std::fabs(((v >> 11) & 31) - eR + 0.5), dG = (int)std::fabs(((v >> 5) & 63) - eG + 0.5);
      worstR = std::max(worstR, dR); worstG = std::max(worstG, dG);
      if (dR > tolR) ++badR;
      if (dG > tolG) ++badG;
    }
  CHECKM(badB == 0, "%s: %d diem sai kenh B (lan anh/ghep nham, mong seed %d)", tag, badB, p.seed);
  CHECKM(badR == 0, "%s: %d diem lech kenh R (hinh hoc cover sai, lech max %d)", tag, badR, worstR);
  CHECKM(badG == 0, "%s: %d diem lech kenh G (hinh hoc cover sai, lech max %d)", tag, badG, worstG);
}

constexpr int FW = 154, FH = 232;  // khung Main Screen

// ================================================================ CAC TEST
static void testFresh() {
  printf("[T1] trang thai ban dau / khong co anh\n");
  resetAll();
  CHECK(!SlideshowService::settled());
  CHECK(SlideshowService::image() == nullptr);
  SlideshowService::request(FW, FH);
  CHECK(SlideshowService::settled());  // khong co file -> NO_PHOTO
  CHECK(curState() == State::NO_PHOTO);
  CHECK(SlideshowService::image() == nullptr);
  CHECK(g_tasksCreated == 0);
  CHECK(g_pathReads == 1);
  SlideshowService::request(FW, FH); SlideshowService::request(FW, FH);
  CHECKM(g_pathReads == 1, "doc lai slideshow.txt %d lan", (int)g_pathReads);
  CHECK(outBytes() == 0);
}

static void testSetPhotoInvalid() {
  printf("[T2] setPhoto voi duong dan khong hop le\n");
  resetAll();
  SlideshowService::setPhoto("/images/a_landscape.jpg");
  const uint32_t gen = curGen(); const int w0 = g_writes;
  CHECK(!SlideshowService::setPhoto(nullptr));
  CHECK(!SlideshowService::setPhoto(""));
  CHECK(!SlideshowService::setPhoto("images/a.jpg"));
  CHECK(!SlideshowService::setPhoto(" /images/a.jpg"));
  std::string p256 = "/" + std::string(255, 'x');  // 256 ky tu
  std::string p255 = "/" + std::string(254, 'x');
  CHECK(!SlideshowService::setPhoto(p256.c_str()));
  CHECK(curGen() == gen && g_writes == w0);  // tu choi thi khong dong cham gi
  CHECK(gPath == "/images/a_landscape.jpg");
  CHECK(SlideshowService::setPhoto(p255.c_str()));  // 255: bien hop le
  CHECK(gPath == p255 && fsText(kSlidePath) == p255);
  CHECK(SlideshowService::setPhoto("/"));  // ngan nhat
  CHECK(gPath == "/");
  resetAll();
}

static void testBasicFlow() {
  printf("[T3] setPhoto -> request -> READY (moi kich thuoc anh)\n");
  for (int i = 0; i < kNPhotos; ++i) {
    resetAll(); g_strict = true;
    const Photo& p = kPhotos[i];
    CHECK(SlideshowService::setPhoto(p.path));
    CHECK(fsText(kSlidePath) == p.path);
    CHECK(!SlideshowService::settled());  // IDLE
    CHECK(SlideshowService::image() == nullptr);
    SlideshowService::request(FW, FH);
    CHECK(waitSettled());
    CHECK(waitTasks());
    checkCover(SlideshowService::image(), FW, FH, p, p.path);
    CHECKM(g_lockViolations == 0, "decode giu gMutex");
    CHECK(g_tasksCreated == 1 && g_returnedNoDelete == 0);
    CHECK(g_lastStack == 8192 && g_lastPrio == 1 && g_lastCore == 0 && g_lastName == "slideshow");
    CHECKM(outCount() == 1, "PSRAM con %zu khoi (mong 1 = anh cache)", outCount());
    CHECKM(outBytes() == (size_t)FW * FH * 2, "PSRAM %zu byte mong %d", outBytes(), FW * FH * 2);
    g_strict = false;
    { auto f = ImageResize::fitInside(p.W, p.H, FW * kDecodeScale, FH * kDecodeScale);
      const double sc = std::max((double)FW / f.w, (double)FH / f.h);
      if (sc > 1.0 && (f.w < p.W || f.h < p.H)) NOTE("T3: %s (%dx%d): anh trung gian chi %dx%d nen bi phong to %.2fx de phu khung %dx%d (mo). Khung giai ma la hop BAO ben trong 4x khung, khong phai hop PHU.", p.path, p.W, p.H, f.w, f.h, sc, FW, FH); }
  }
  resetAll();
}

static void testPersistence() {
  printf("[T4] doc duong dan sau khi khoi dong lai / dinh dang file\n");
  struct V { const char* name; std::string content; bool expectPhoto; };
  std::vector<V> vs = {
      {"sach", "/images/c_hd.jpg", true},
      {"\\n cuoi", "/images/c_hd.jpg\n", true},
      {"\\r\\n cuoi", "/images/c_hd.jpg\r\n", true},
      {"nhieu khoang trang+newline", "/images/c_hd.jpg \r\n \n", true},
      {"chi newline", "\n", false},
      {"chi khoang trang", "   ", false},
      {"rong", "", false},
      {"khong phai duong dan", "hello", true},  // vao IDLE roi FAILED (khong co file)
  };
  for (auto& v : vs) {
    resetAll();
    fsPut(kSlidePath, v.content);
    SlideshowService::request(FW, FH);
    const bool decodeAttempt = curState() == State::LOADING || g_tasksCreated > 0;
    CHECKM(decodeAttempt == v.expectPhoto, "[%s] co thu giai ma=%d mong %d", v.name, decodeAttempt, v.expectPhoto);
    CHECK(waitSettled()); CHECK(waitTasks());
    if (!strcmp(v.name, "khong phai duong dan")) {
      CHECK(curState() == State::FAILED && SlideshowService::image() == nullptr);
    } else if (v.expectPhoto) {
      checkCover(SlideshowService::image(), FW, FH, kPhotos[2], v.name);
    } else {
      CHECK(curState() == State::NO_PHOTO);
    }
  }
  // File > 255 byte: bi cat 255 ky tu -> duong dan sai -> FAILED, khong tran bo dem
  resetAll();
  fsPut(kSlidePath, "/" + std::string(400, 'y'));
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  CHECK(curState() == State::FAILED);
  CHECK(gPath.size() == 255);
  // Request lan hai thu lai (FAILED -> thu lai) va van khong sao
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  CHECK(g_tasksCreated == 2);
  // The SD loi luc doc duong dan -> NO_PHOTO (va dinh luon)
  resetAll();
  SlideshowService::setPhoto(kPhotos[0].path);
  resetService();           // "khoi dong lai", file con tren the
  g_sdReadFail = true;
  SlideshowService::request(FW, FH);
  CHECK(curState() == State::NO_PHOTO);
  g_sdReadFail = false;
  SlideshowService::request(FW, FH);
  CHECK(curState() == State::NO_PHOTO);
  NOTE("T4: khi doc /data/slideshow.txt loi (the vang luc khoi dong) service vao NO_PHOTO va KHONG doc lai trong phien nay.");
  // Khoi dong lai binh thuong
  resetService();
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), FW, FH, kPhotos[0], "reboot");
  resetAll();
}

static void testWriteFailure() {
  printf("[T5] ghi the SD loi\n");
  resetAll();
  SlideshowService::setPhoto(kPhotos[0].path);
  g_sdWriteFail = true;
  CHECK(!SlideshowService::setPhoto(kPhotos[1].path));   // tra false...
  CHECK(fsText(kSlidePath) == kPhotos[0].path);          // the khong doi
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), FW, FH, kPhotos[1], "ap dung trong RAM");  // ...nhung RAM ap dung
  g_sdWriteFail = false;
  resetAll();
}

static void testClear() {
  printf("[T6] clearPhoto\n");
  resetAll();
  SlideshowService::clearPhoto();  // chua tung nap
  CHECK(SlideshowService::settled() && !SlideshowService::image());
  CHECK(fsText(kSlidePath) == "");
  SlideshowService::setPhoto(kPhotos[0].path);
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  CHECK(SlideshowService::image() != nullptr);
  SlideshowService::clearPhoto();
  CHECK(SlideshowService::image() == nullptr && SlideshowService::settled());
  CHECKM(outBytes() == 0, "clearPhoto khong tra PSRAM: %zu", outBytes());
  SlideshowService::request(FW, FH);   // NO_PHOTO: khong lam gi
  CHECK(g_tasksCreated == 1);
  resetService();                       // khoi dong lai: file rong -> khong co anh
  SlideshowService::request(FW, FH);
  CHECK(curState() == State::NO_PHOTO && g_tasksCreated == 1);
  resetAll();
}

static void testRequestSizes() {
  printf("[T7] request: cache dung lai / doi kich thuoc / tham so la\n");
  resetAll(); g_strict = true;
  SlideshowService::setPhoto(kPhotos[0].path);
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  const lv_img_dsc_t* d1 = SlideshowService::image();
  const uint8_t* data1 = d1->data;
  { // setPhoto giai phong cache cu NGAY (khong doi toi lan giai ma sau)
    SlideshowService::setPhoto(kPhotos[1].path);
    CHECKM(outBytes() == 0, "setPhoto khong giai phong cache cu: %zu byte", outBytes());
    SlideshowService::setPhoto(kPhotos[0].path);
    SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
    d1 = SlideshowService::image(); data1 = d1->data; }
  const int imgReads = g_imgReads;
  for (int i = 0; i < 20; ++i) { SlideshowService::request(FW, FH); CHECK(SlideshowService::settled()); }
  CHECK(g_tasksCreated == 2 && g_imgReads == imgReads);       // vao/ra Main khong giai ma lai
  CHECK(SlideshowService::image() == d1 && d1->data == data1);  // con tro on dinh
  // doi kich thuoc: giai ma lai, giai phong ban cu
  SlideshowService::request(100, 100);
  CHECK(!SlideshowService::settled());
  CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), 100, 100, kPhotos[0], "100x100");
  CHECKM(outCount() == 1, "doi kich thuoc roi con %zu khoi", outCount());
  // chi doi 1 canh
  SlideshowService::request(100, 101); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), 100, 101, kPhotos[0], "100x101");
  SlideshowService::request(99, 101); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), 99, 101, kPhotos[0], "99x101");
  CHECK(g_tasksCreated == 5);
  // kich thuoc cuc tri nho
  SlideshowService::request(1, 1); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), 1, 1, kPhotos[0], "1x1");
  SlideshowService::request(1, 300); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), 1, 300, kPhotos[0], "1x300", 12, 12);  // khung suy bien: trung gian chi 4x3 px
  SlideshowService::request(300, 1); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), 300, 1, kPhotos[0], "300x1", 12, 12);
  // tham so khong hop le -> FAILED, khong sap, khong ro ri
  const int bad[][2] = {{0, 0}, {-5, 10}, {10, -5}, {0, 232}, {154, 0}};
  for (auto& b : bad) {
    SlideshowService::request(b[0], b[1]);
    CHECK(waitSettled()); CHECK(waitTasks());
    CHECKM(SlideshowService::image() == nullptr, "request(%d,%d) van co anh", b[0], b[1]);
  }
  CHECKM(g_lockViolations == 0, "decode giu gMutex");
  g_strict = false;
  // khung lon: bitfield w:11 h:11 cua lv_img_header_t cat bot
  SlideshowService::request(3000, 100); CHECK(waitSettled()); CHECK(waitTasks());
  const lv_img_dsc_t* d = SlideshowService::image();
  if (d) {
    CHECK(d->data_size == 3000u * 100u * 2u);
    if (d->header.w != 3000) NOTE("T7: khung >2047 px: lv_img_header_t.w chi 11 bit nen header.w=%u (data_size van dung). Khung that 154x232 khong bi anh huong.", (unsigned)d->header.w);
  }
  SlideshowService::clearPhoto();
  CHECK(outBytes() == 0);
  resetAll();
}

static void testFailures() {
  printf("[T8] giai ma loi -> FAILED -> thu lai\n");
  struct F { const char* name; std::function<void()> make; };
  std::vector<F> fs = {
      {"khong co file", [] { std::lock_guard<std::mutex> l(g_fsm); g_fs.erase("/images/a_landscape.jpg"); }},
      {"file rong", [] { fsPut("/images/a_landscape.jpg", ""); }},
      {"rac (khong phai JPG)", [] { fsPut("/images/a_landscape.jpg", "not a jpeg at all"); }},
      {"cut ngan", [] { fsPut("/images/a_landscape.jpg", "FJPG\x10"); }},
      {"kich thuoc 0", [] { putJpg("/images/a_landscape.jpg", 0, 10, 3); }},
      {"qua lon (>16384)", [] { putJpg("/images/a_landscape.jpg", 20000, 10, 3); }},
      {"loi giua chung", [] { putJpg("/images/a_landscape.jpg", 4000, 3000, 3, 1); }},
  };
  for (auto& f : fs) {
    resetAll(); g_strict = true;
    f.make();
    SlideshowService::setPhoto("/images/a_landscape.jpg");
    SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
    CHECKM(curState() == State::FAILED, "[%s] state=%d", f.name, (int)curState());
    CHECKM(SlideshowService::image() == nullptr, "[%s] image() khac null", f.name);
    CHECKM(outBytes() == 0, "[%s] ro ri PSRAM %zu byte", f.name, outBytes());
    CHECK(g_returnedNoDelete == 0);
    // sua file roi thu lai: FAILED -> READY
    putJpg("/images/a_landscape.jpg", 4000, 3000, 3);
    SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
    checkCover(SlideshowService::image(), FW, FH, kPhotos[0], f.name);
    CHECK(g_lockViolations == 0);
    g_strict = false;
  }
  // SD doc loi luc giai ma
  resetAll();
  SlideshowService::setPhoto(kPhotos[0].path);
  g_sdReadFail = true;
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  CHECK(curState() == State::FAILED);
  g_sdReadFail = false;
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), FW, FH, kPhotos[0], "sd phuc hoi");
  resetAll();
}

static void testStale() {
  printf("[T9] ket qua job cu bi bo (setPhoto/clearPhoto/doi kich thuoc luc dang giai ma)\n");
  // (a) setPhoto trong luc giai ma
  { resetAll(); g_strict = true;
    Gate g; g_readHook = [&](const char*) { g.pass(); };
    SlideshowService::setPhoto(kPhotos[0].path);
    g.close(); SlideshowService::request(FW, FH);
    CHECK(g.waitBlocked(1)); CHECK(curState() == State::LOADING);
    CHECK(!SlideshowService::settled());
    SlideshowService::setPhoto(kPhotos[1].path);   // job cu gen cu
    g.release(); CHECK(waitTasks());
    CHECK(SlideshowService::image() == nullptr);   // anh cu bi bo, KHONG ghi de
    CHECK(curState() == State::IDLE);
    CHECKM(outBytes() == 0, "(a) anh cu bi bo nhung khong giai phong: %zu", outBytes());
    SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
    checkCover(SlideshowService::image(), FW, FH, kPhotos[1], "(a) anh moi");
    CHECK(g_lockViolations == 0); g_strict = false; g_readHook = nullptr; }
  // (b) clearPhoto trong luc giai ma
  { resetAll();
    Gate g; g_readHook = [&](const char*) { g.pass(); };
    SlideshowService::setPhoto(kPhotos[2].path);
    g.close(); SlideshowService::request(FW, FH); CHECK(g.waitBlocked(1));
    SlideshowService::clearPhoto();
    g.release(); CHECK(waitTasks());
    CHECK(SlideshowService::image() == nullptr && curState() == State::NO_PHOTO && SlideshowService::settled());
    CHECKM(outBytes() == 0, "(b) ro ri %zu", outBytes()); g_readHook = nullptr; }
  // (c) request lan 2 luc dang LOADING khong tao job thu hai; doi kich thuoc luc LOADING bi bo qua
  { resetAll();
    Gate g; g_readHook = [&](const char*) { g.pass(); };
    SlideshowService::setPhoto(kPhotos[3].path);
    g.close(); SlideshowService::request(FW, FH); CHECK(g.waitBlocked(1));
    SlideshowService::request(FW, FH); SlideshowService::request(50, 50);
    CHECK(g_tasksCreated == 1);
    g.release(); CHECK(waitSettled()); CHECK(waitTasks());
    checkCover(SlideshowService::image(), FW, FH, kPhotos[3], "(c)");
    NOTE("T9c: request(kich thuoc khac) luc dang LOADING bi bo qua; anh cuoi co kich thuoc cu den lan request ke tiep (Main goi request moi lan onCreate nen tu sua).");
    g_readHook = nullptr; }
  // (d) setPhoto roi request khi job cu van chay: 2 job song song, ImageService phai tuan tu hoa
  { resetAll(); g_strict = true;
    Gate g; g_readHook = [&](const char*) { g.pass(); };
    SlideshowService::setPhoto(kPhotos[0].path);
    g.close(); SlideshowService::request(FW, FH); CHECK(g.waitBlocked(1));
    SlideshowService::setPhoto(kPhotos[1].path);
    SlideshowService::request(FW, FH);   // job 2 (gen moi) khi job 1 con treo
    CHECK(g.waitBlocked(2));
    CHECK(g_tasksCreated == 2);
    g.release(); CHECK(waitSettled()); CHECK(waitTasks());
    checkCover(SlideshowService::image(), FW, FH, kPhotos[1], "(d)");
    CHECK(outCount() == 1);
    CHECKM(g_maxDrawing == 1, "ImageService cho 2 task giai ma cung luc (%d)", (int)g_maxDrawing);
    CHECK(g_lockViolations == 0); g_strict = false; g_readHook = nullptr; }
  // (e) doi kich thuoc luc READY roi doi anh luc dang giai ma kich thuoc moi
  { resetAll();
    SlideshowService::setPhoto(kPhotos[0].path);
    SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
    Gate g; g_readHook = [&](const char*) { g.pass(); };
    g.close(); SlideshowService::request(100, 100); CHECK(g.waitBlocked(1));
    CHECK(SlideshowService::image() == nullptr);  // cache cu da bi bo ngay khi doi kich thuoc
    SlideshowService::clearPhoto();
    g.release(); CHECK(waitTasks());
    CHECK(SlideshowService::image() == nullptr && outBytes() == 0); g_readHook = nullptr; }
  resetAll();
}

static void testTaskCreateFail() {
  printf("[T10] khong tao duoc task\n");
  resetAll();
  SlideshowService::setPhoto(kPhotos[0].path);
  g_taskCreateFail = true;
  SlideshowService::request(FW, FH);
  CHECK(curState() == State::FAILED && SlideshowService::settled() && !SlideshowService::image());
  CHECK(outBytes() == 0);   // Job dung new: ro ri se do LeakSanitizer bat khi thoat
  g_taskCreateFail = false;
  SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
  checkCover(SlideshowService::image(), FW, FH, kPhotos[0], "tao task lai");
  resetAll();
}

static void testAllocFailures() {
  printf("[T11] het PSRAM tai tung diem cap phat\n");
  int fallback = 0, failed = 0, ok = 0;
  for (int pi : {0, 4}) {
    for (int n = 1; n <= 8; ++n) {
      resetAll(); g_strict = true;
      SlideshowService::setPhoto(kPhotos[pi].path);
      g_allocCalls = 0; g_allocFailAt = n;
      SlideshowService::request(FW, FH); CHECK(waitSettled()); CHECK(waitTasks());
      g_allocFailAt = 0;
      const lv_img_dsc_t* d = SlideshowService::image();
      if (d) {
        const uint32_t wH = d->header.w, hH = d->header.h;
        CHECKM(d->data && d->data_size == wH * hH * 2u, "n=%d: header/data_size khong khop", n);
        if ((int)wH == FW && (int)hH == FH) {
          checkCover(d, FW, FH, kPhotos[pi], "alloc");
          ++ok;
        } else {
          ++fallback;   // dung anh goc khi het bo nho
          const uint8_t* b = d->data; (void)b;
          volatile uint8_t sink = 0; for (uint32_t i = 0; i < d->data_size; i += 97) sink ^= d->data[i]; (void)sink;
        }
      } else {
        ++failed; CHECK(curState() == State::FAILED);
      }
      SlideshowService::clearPhoto();
      CHECKM(outBytes() == 0, "pi=%d n=%d: ro ri %zu byte", pi, n, outBytes());
      CHECK(g_badFrees == 0 && g_lockViolations == 0);
      g_strict = false;
    }
  }
  printf("    ket qua: dung khung %d, dung anh goc %d, FAILED %d\n", ok, fallback, failed);
  CHECK(fallback > 0 && failed > 0 && ok > 0);  // ca ba nhanh deu da duoc chay
  // het PSRAM khi doc duong dan
  resetAll();
  SlideshowService::setPhoto(kPhotos[0].path);
  resetService();
  g_allocFailAll = true; SlideshowService::request(FW, FH); g_allocFailAll = false;
  CHECK(curState() == State::NO_PHOTO);
  resetAll();
}

static uint16_t refBilinear(const std::vector<uint16_t>& s, int sw, int sh, double fx, double fy, int ch) {
  int ix = (int)fx, iy = (int)fy; int ix1 = std::min(ix + 1, sw - 1), iy1 = std::min(iy + 1, sh - 1);
  double ax = fx - ix, ay = fy - iy;
  auto c = [&](int x, int y) { uint16_t v = s[(size_t)y * sw + x]; return ch == 0 ? (v >> 11) & 31 : ch == 1 ? (v >> 5) & 63 : v & 31; };
  return (uint16_t)std::floor(c(ix, iy) * (1 - ax) * (1 - ay) + c(ix1, iy) * ax * (1 - ay) + c(ix, iy1) * (1 - ax) * ay + c(ix1, iy1) * ax * ay + 1e-9);
}
static void testCoverResample() {
  printf("[T12] coverResample doi chieu voi tham chieu double (ngau nhien + bien)\n");
  std::mt19937 rng(12345);
  int worst = 0, cases = 0;
  std::vector<std::pair<int, int>> dims = {{1, 1}, {1, 9}, {9, 1}, {2, 2}, {3, 7}, {64, 48}, {231, 17}, {17, 231}, {500, 500}};
  for (int t = 0; t < 40; ++t) dims.push_back({1 + (int)(rng() % 300), 1 + (int)(rng() % 300)});
  const int tw[][2] = {{154, 232}, {1, 1}, {1, 50}, {50, 1}, {320, 240}, {7, 7}};
  for (auto& sd : dims)
    for (auto& tg : tw) {
      const int sw = sd.first, sh = sd.second, w = tg[0], h = tg[1];
      Image src;
      src.pixels = static_cast<uint16_t*>(Psram::alloc((size_t)sw * sh * 2));  // dung co, ASan bat doc tran
      src.w = sw; src.h = sh;
      std::vector<uint16_t> ref((size_t)sw * sh);
      for (size_t i = 0; i < ref.size(); ++i) ref[i] = src.pixels[i] = (uint16_t)rng();
      Image dst;
      CHECK(coverResample(src, w, h, &dst));
      CHECK(dst.w == w && dst.h == h);
      const double s = std::max((double)w / sw, (double)h / sh);
      const double x0 = (sw - w / s) / 2, y0 = (sh - h / s) / 2;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          double fx = std::min(std::max(x0 + (x + 0.5) / s - 0.5, 0.0), sw - 1.0);
          double fy = std::min(std::max(y0 + (y + 0.5) / s - 0.5, 0.0), sh - 1.0);
          const uint16_t v = dst.pixels[(size_t)y * w + x];
          for (int ch = 0; ch < 3; ++ch) {
            const int got = ch == 0 ? (v >> 11) & 31 : ch == 1 ? (v >> 5) & 63 : v & 31;
            const int d = std::abs(got - (int)refBilinear(ref, sw, sh, fx, fy, ch));
            worst = std::max(worst, d);
            if (d > 2) { CHECKM(false, "src %dx%d -> %dx%d (%d,%d) kenh %d lech %d", sw, sh, w, h, x, y, ch, d); goto next; }
          }
        }
      ++g_pass; ++cases;
    next:
      Psram::release(dst.pixels); Psram::release(src.pixels);
    }
  printf("    %d to hop, lech toi da so voi tham chieu: %d muc mau\n", cases, worst);
  CHECK(worst <= 2);
  // tham so loi
  Image e, d2; CHECK(!coverResample(e, 10, 10, &d2));
  Image z; z.pixels = static_cast<uint16_t*>(Psram::alloc(4)); z.w = 0; z.h = 1; CHECK(!coverResample(z, 10, 10, &d2)); Psram::release(z.pixels);
  CHECK(outBytes() == 0);
}

static void testResizePure() {
  printf("[T13] image_resize (fitInside / chooseJpegScale / boxDownscale)\n");
  std::mt19937 rng(777);
  for (int t = 0; t < 20000; ++t) {
    int sw = 1 + rng() % 9000, sh = 1 + rng() % 9000, mw = 1 + rng() % 700, mh = 1 + rng() % 700;
    auto f = ImageResize::fitInside(sw, sh, mw, mh);
    CHECK(f.w >= 1 && f.h >= 1 && f.w <= std::max(sw, 1) && f.h <= std::max(sh, 1));
    CHECK(f.w <= mw || sw <= mw); CHECK(f.h <= mh || sh <= mh);
    if (sw <= mw && sh <= mh) CHECK(f.w == sw && f.h == sh);
    else CHECK(f.w == mw || f.h == mh);
    const int sc = ImageResize::chooseJpegScale(sw, sh, f);
    CHECK(sc == 1 || sc == 2 || sc == 4 || sc == 8);
    CHECK(sw / sc >= f.w && sh / sc >= f.h);
    if (sc < 8) { int up = sc == 1 ? 2 : sc * 2; CHECK(!(sw / up >= f.w && sh / up >= f.h)); }
  }
  auto z = ImageResize::fitInside(0, 5, 5, 5); CHECK(z.w == 0 && z.h == 0);
  auto big = ImageResize::fitInside(2000000000, 1, 100, 100); CHECK(big.w == 100 && big.h >= 1);
  // box filter: anh phang giu nguyen, dung bo nho, trung binh
  for (int t = 0; t < 300; ++t) {
    int sw = 1 + rng() % 120, sh = 1 + rng() % 120, dw = 1 + rng() % sw, dh = 1 + rng() % sh;
    uint16_t* s = static_cast<uint16_t*>(Psram::alloc((size_t)sw * sh * 2));
    uint16_t* d = static_cast<uint16_t*>(Psram::alloc((size_t)dw * dh * 2));
    const uint16_t c = (uint16_t)rng();
    for (int i = 0; i < sw * sh; ++i) s[i] = c;
    ImageResize::boxDownscale(s, sw, sh, d, dw, dh);
    for (int i = 0; i < dw * dh; ++i) if (d[i] != c) { CHECKM(false, "anh phang %04x -> %04x (%dx%d->%dx%d)", c, d[i], sw, sh, dw, dh); break; }
    Psram::release(s); Psram::release(d);
  }
  CHECK(outBytes() == 0);
}

// Ket cau doi tuong chuoi tac vu ngau nhien, hop dong: Album (setPhoto/clearPhoto) chi chay khi Main khong hien.
static void testStress(int ms) {
  printf("[T14] da luong: Main (request/image) + Album (setPhoto/clear) + SD loi, %d ms\n", ms);
  resetAll();
  RwLock contract;   // Main giu shared khi doc anh; Album giu exclusive khi doi/xoa anh
  std::atomic<bool> stop{false};
  std::atomic<int> reads{0}, bad{0}, ready{0};
  std::thread ui([&] {
    while (!stop) {
      { SharedGuard sl(contract);
        SlideshowService::request(FW, FH);
        const lv_img_dsc_t* d = SlideshowService::image();
        if (d && d->data) {
          ++ready;
          if ((int)d->header.w != FW || (int)d->header.h != FH || d->data_size != (uint32_t)FW * FH * 2) ++bad;
          else {
            const uint16_t* px = reinterpret_cast<const uint16_t*>(d->data);
            const int seed = px[0] & 31;
            for (int i = 0; i < FW * FH; i += 53) if ((int)(px[i] & 31) != seed) { ++bad; break; }
            bool known = false; for (auto& p : kPhotos) if (p.seed == seed) known = true;
            if (!known) ++bad;
          }
          ++reads;
        }
        (void)SlideshowService::settled(); }
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  });
  std::thread album([&] {
    std::mt19937 r(99);
    while (!stop) {
      { ExclGuard xl(contract);
        const int a = r() % 10;
        if (a == 0) SlideshowService::clearPhoto();
        else SlideshowService::setPhoto(kPhotos[r() % kNPhotos].path);
        if (a == 1) SlideshowService::setPhoto("khong-hop-le"); }
      if (r() % 5 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(40)); else std::this_thread::sleep_for(std::chrono::microseconds(100 + r() % 2000));
    }
  });
  std::thread sdflaky([&] {
    std::mt19937 r(5);
    while (!stop) { g_sdWriteFail = (r() % 4 == 0); std::this_thread::sleep_for(std::chrono::microseconds(500)); }
    g_sdWriteFail = false;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  stop = true; ui.join(); album.join(); sdflaky.join();
  CHECK(waitTasks(15000));
  CHECKM(bad == 0, "%d lan doc anh thay du lieu hong/lan lon", (int)bad);
  CHECKM(g_maxDrawing <= 1, "ImageService cho %d giai ma cung luc", (int)g_maxDrawing);
  CHECK(g_returnedNoDelete == 0 && g_badFrees == 0);
  printf("    %d lan thay anh san sang, %d task, khong hong: %s\n", (int)ready, (int)g_tasksCreated, bad == 0 ? "dung" : "SAI");
  SlideshowService::clearPhoto();
  CHECKM(outBytes() == 0, "sau stress ro ri %zu byte (%zu khoi)", outBytes(), outCount());
  resetAll();
}

int main(int argc, char** argv) {
  int stressMs = 1500;
  for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--stress-ms") && i + 1 < argc) stressMs = atoi(argv[++i]);
  const bool verbose = argc > 1 && !strcmp(argv[1], "-v");
  Serial.quiet = !verbose;
  testFresh();
  testSetPhotoInvalid();
  testBasicFlow();
  testPersistence();
  testWriteFailure();
  testClear();
  testRequestSizes();
  testFailures();
  testStale();
  testTaskCreateFail();
  testAllocFailures();
  testCoverResample();
  testResizePure();
  testStress(stressMs);
  CHECK(g_delayCalls == 0);
  CHECK(g_badFrees == 0);
  CHECK(waitTasks());
  printf("\n=== %s: dat %d, hong %d, ghi chu %d ===\n", g_fail ? "CO LOI" : "TAT CA DAT", g_pass, g_fail, g_notes);
  return g_fail ? 1 : 0;
}
