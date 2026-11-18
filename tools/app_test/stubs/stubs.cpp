// Cai dat gia lap cho test B3: heap_caps, FreeRTOS (pthread), Serial, SD trong bo nho, TimeService,
// TJpgDec gia, man hinh gia (kiem tra vung ve), icon, keypad, LvglPort/LvglLock.
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include <lvgl.h>
#include "Arduino.h"
#include "TJpg_Decoder.h"
#include "assets/icons/icons.hpp"
#include "core/lvgl_port.hpp"
#include "core/psram.hpp"
#include "config/display.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "services/keypad_matrix.hpp"
#include "services/sd_storage.hpp"
#include "services/time_service.hpp"
#include "test_hooks.hpp"

namespace hooks {
std::atomic<uint32_t> nowMs(0);
std::atomic<size_t> failMinSize(1024);
std::atomic<long> heapBytes(0), heapBlocks(0), heapAllocs(0), failAfter(-1), failCount(0);
std::atomic<int> liveTasks(0), tasksCreated(0), taskCreateFail(0), lockViolations(0);
std::atomic<long> flushCount(0), flushBadArea(0);
std::atomic<int> sdMounted(1), sdWriteFail(0), sdReadDelayMs(0);
std::atomic<long> sdWrites(0);
std::atomic<int> decodeDelayMs(0);
std::atomic<long> decodeCalls(0);
std::atomic<int> tYear(2026), tMonth(11), tDay(16), tHour(7), tMin(5);
static std::mutex g_fsm;
static std::map<std::string, std::vector<uint8_t>> g_fs;
void fsClear() { std::lock_guard<std::mutex> l(g_fsm); g_fs.clear(); }
void fsPut(const std::string& p, const std::vector<uint8_t>& d) { std::lock_guard<std::mutex> l(g_fsm); g_fs[p] = d; }
void fsPutStr(const std::string& p, const std::string& s) { fsPut(p, std::vector<uint8_t>(s.begin(), s.end())); }
bool fsGet(const std::string& p, std::vector<uint8_t>* o) {
  std::lock_guard<std::mutex> l(g_fsm); auto it = g_fs.find(p); if (it == g_fs.end()) return false; *o = it->second; return true;
}
void putJpg(const char* path, int W, int H, int seed, int mode) {
  std::vector<uint8_t> b = {'F', 'J', 'P', 'G', (uint8_t)W, (uint8_t)(W >> 8), (uint8_t)H, (uint8_t)(H >> 8),
                            (uint8_t)seed, 0, 0, 0, (uint8_t)mode, 1, 2, 3};
  fsPut(path, b);
}
}  // namespace hooks

// ---------------- Arduino ----------------
extern "C" uint32_t millis(void) { return hooks::nowMs.load(); }
SerialStub Serial;
EspStub ESP;
std::atomic<int> g_delayCalls(0);
int SerialStub::available() { std::lock_guard<std::mutex> l(m_); return (int)q_.size(); }
int SerialStub::read() { std::lock_guard<std::mutex> l(m_); if (q_.empty()) return -1; int c = q_.front(); q_.pop_front(); return c; }
void SerialStub::feed(char c) { std::lock_guard<std::mutex> l(m_); q_.push_back(c); }

// ---------------- heap_caps (LVGL + PSRAM) ----------------
namespace { struct Hdr { size_t size; size_t pad; }; }
extern "C" void* heap_caps_malloc(size_t size, unsigned) {
  Hdr* h = (Hdr*)malloc(sizeof(Hdr) + size);
  if (!h) return nullptr;
  h->size = size;
  hooks::heapBytes += (long)size; ++hooks::heapBlocks; ++hooks::heapAllocs;
  return h + 1;
}
// Chi Psram::alloc (psram.cpp bien dich voi -Dheap_caps_malloc=psram_caps_malloc) bi tiem loi het bo nho.
// LVGL tu treo khi het bo nho (LV_ASSERT_MALLOC), nen khong tiem vao LVGL.
extern "C" void* psram_caps_malloc(size_t size, unsigned caps) {
  long n = hooks::failAfter.load();
  if (n == 0) { ++hooks::failCount; return nullptr; }
  if (n > 0) hooks::failAfter.compare_exchange_strong(n, n - 1);
  return heap_caps_malloc(size, caps);
}
extern "C" void heap_caps_free(void* p) {
  if (!p) return;
  Hdr* h = (Hdr*)p - 1;
  hooks::heapBytes -= (long)h->size; --hooks::heapBlocks;
  free(h);
}
extern "C" void* heap_caps_realloc(void* p, size_t size, unsigned caps) {
  if (!p) return heap_caps_malloc(size, caps);
  if (size == 0) { heap_caps_free(p); return nullptr; }
  void* q = heap_caps_malloc(size, caps);
  if (!q) return nullptr;
  Hdr* h = (Hdr*)p - 1;
  memcpy(q, p, std::min(h->size, size));
  heap_caps_free(p);
  return q;
}
extern "C" size_t heap_caps_get_free_size(unsigned) { return 8u * 1024 * 1024 - (size_t)hooks::heapBytes.load(); }

// ---------------- FreeRTOS ----------------
namespace {
struct Sem {
  std::mutex m; std::condition_variable cv;
  bool binary = true; bool recursive = false;
  int count = 0;                       // nhi phan: 0/1
  std::thread::id owner; int depth = 0;
};
struct Queue {
  std::mutex m; std::condition_variable cv;
  std::deque<std::vector<uint8_t>> items; uint32_t cap, isz;
};
struct TaskArg { TaskFunction_t fn; void* arg; };
void* taskTramp(void* p) {
  TaskArg a = *(TaskArg*)p; delete (TaskArg*)p;
  a.fn(a.arg);
  --hooks::liveTasks;   // task tra ve khong goi vTaskDelete: tren that se abort
  return nullptr;
}
using Clock = std::chrono::steady_clock;
}  // namespace
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, UBaseType_t, TaskHandle_t* h, BaseType_t) {
  if (hooks::taskCreateFail.load()) return pdFAIL;
  pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  ++hooks::liveTasks; ++hooks::tasksCreated;
  if (pthread_create(&t, &at, taskTramp, new TaskArg{fn, arg}) != 0) { --hooks::liveTasks; return pdFAIL; }
  if (h) *h = (TaskHandle_t)(uintptr_t)t;
  return pdPASS;
}
void vTaskDelete(TaskHandle_t) { --hooks::liveTasks; pthread_exit(nullptr); }
SemaphoreHandle_t xSemaphoreCreateMutex() { Sem* s = new Sem; s->count = 1; return s; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { Sem* s = new Sem; s->count = 1; s->recursive = true; return s; }
SemaphoreHandle_t xSemaphoreCreateBinary() { Sem* s = new Sem; s->count = 0; return s; }
static bool waitFor(std::unique_lock<std::mutex>& l, std::condition_variable& cv, uint32_t ticks, std::function<bool()> pred) {
  if (ticks == portMAX_DELAY) { cv.wait(l, pred); return true; }
  return cv.wait_for(l, std::chrono::milliseconds(ticks), pred);
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, uint32_t t) {
  Sem* s = (Sem*)h; std::unique_lock<std::mutex> l(s->m);
  if (!waitFor(l, s->cv, t, [&] { return s->count > 0; })) return pdFALSE;
  s->count = 0; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  Sem* s = (Sem*)h; std::lock_guard<std::mutex> l(s->m); s->count = 1; s->cv.notify_one(); return pdTRUE;   // notify TRONG khoa: ben nhan co the xoa semaphore ngay sau khi Take xong
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, uint32_t t) {
  Sem* s = (Sem*)h; std::unique_lock<std::mutex> l(s->m);
  auto me = std::this_thread::get_id();
  if (s->depth > 0 && s->owner == me) { ++s->depth; return pdTRUE; }
  if (!waitFor(l, s->cv, t, [&] { return s->depth == 0; })) return pdFALSE;
  s->owner = me; s->depth = 1; return pdTRUE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) {
  Sem* s = (Sem*)h; std::lock_guard<std::mutex> l(s->m);
  if (s->depth == 0 || s->owner != std::this_thread::get_id()) { ++hooks::lockViolations; return pdFALSE; }
  if (--s->depth == 0) s->cv.notify_one();
  return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t h) { delete (Sem*)h; }
QueueHandle_t xQueueCreate(uint32_t len, uint32_t isz) { Queue* q = new Queue; q->cap = len; q->isz = isz; return q; }
static BaseType_t qSend(QueueHandle_t h, const void* it, uint32_t t, bool front) {
  Queue* q = (Queue*)h; std::unique_lock<std::mutex> l(q->m);
  if (!waitFor(l, q->cv, t, [&] { return q->items.size() < q->cap; })) return pdFALSE;
  std::vector<uint8_t> v((const uint8_t*)it, (const uint8_t*)it + q->isz);
  if (front) q->items.push_front(v); else q->items.push_back(v);
  q->cv.notify_all(); return pdTRUE;
}
BaseType_t xQueueSendToBack(QueueHandle_t h, const void* it, uint32_t t) { return qSend(h, it, t, false); }
BaseType_t xQueueSendToFront(QueueHandle_t h, const void* it, uint32_t t) { return qSend(h, it, t, true); }
BaseType_t xQueueReceive(QueueHandle_t h, void* it, uint32_t t) {
  Queue* q = (Queue*)h; std::unique_lock<std::mutex> l(q->m);
  if (!waitFor(l, q->cv, t, [&] { return !q->items.empty(); })) return pdFALSE;
  memcpy(it, q->items.front().data(), q->isz); q->items.pop_front(); q->cv.notify_all(); return pdTRUE;
}
void vQueueDelete(QueueHandle_t h) { delete (Queue*)h; }

// ---------------- SD trong bo nho ----------------
namespace SdStorage {
bool begin() { return true; }
bool isMounted() { return hooks::sdMounted.load() != 0; }
bool exists(const char* p) { std::vector<uint8_t> v; return isMounted() && hooks::fsGet(p, &v); }
static void sdDelay() { int d = hooks::sdReadDelayMs.load(); if (d > 0) usleep(d * 1000); }
std::vector<std::string> listFiles(const char* dir, const char* exts) {
  std::vector<std::string> out;
  if (!isMounted()) return out;
  sdDelay();
  std::string prefix = std::string(dir) + "/", ex = exts ? exts : "";
  std::vector<std::string> names;
  { std::lock_guard<std::mutex> l(hooks::g_fsm);
    for (auto& kv : hooks::g_fs) if (kv.first.compare(0, prefix.size(), prefix) == 0 && kv.first.find('/', prefix.size()) == std::string::npos) names.push_back(kv.first); }
  for (auto& n : names) {
    std::string base = n.substr(prefix.size());
    if (base.empty() || base[0] == '.') continue;
    size_t dot = base.rfind('.');
    if (!ex.empty()) {
      if (dot == std::string::npos) continue;
      std::string e = base.substr(dot); std::transform(e.begin(), e.end(), e.begin(), ::tolower);
      if ((";" + ex + ";").find(";" + e + ";") == std::string::npos) continue;
    }
    out.push_back(n);
  }
  std::sort(out.begin(), out.end());
  return out;
}
void* readFileToPsram(const char* path, size_t* outSize) {
  if (outSize) *outSize = 0;
  if (!isMounted()) return nullptr;
  sdDelay();
  std::vector<uint8_t> v;
  if (!hooks::fsGet(path, &v) || v.empty()) return nullptr;
  void* p = Psram::alloc(v.size());
  if (!p) return nullptr;
  memcpy(p, v.data(), v.size()); if (outSize) *outSize = v.size();
  return p;
}
bool writeFile(const char* path, const void* data, size_t len) {
  if (!isMounted() || hooks::sdWriteFail.load()) return false;
  ++hooks::sdWrites;
  hooks::fsPut(path, std::vector<uint8_t>((const uint8_t*)data, (const uint8_t*)data + len));
  return true;
}
}  // namespace SdStorage

// ---------------- TimeService ----------------
namespace TimeService {
DateTime now() {
  DateTime d; d.year = (uint16_t)hooks::tYear; d.month = (uint8_t)hooks::tMonth; d.day = (uint8_t)hooks::tDay;
  d.hour = (uint8_t)hooks::tHour; d.minute = (uint8_t)hooks::tMin; d.second = 0; return d;
}
}

// ---------------- TJpgDec gia (giong B2) ----------------
uint16_t fakeJpgPixel(uint32_t seed, int x, int y, int W, int H) {
  const uint32_t r = W > 1 ? (uint32_t)x * 31u / (uint32_t)(W - 1) : 0;
  const uint32_t g = H > 1 ? (uint32_t)y * 63u / (uint32_t)(H - 1) : 0;
  return (uint16_t)((r << 11) | (g << 5) | (seed & 31u));
}
TJpgDecoder TJpgDec;
JRESULT TJpgDecoder::getJpgSize(uint16_t* w, uint16_t* h, const uint8_t* d, uint32_t n) {
  if (n < 13 || memcmp(d, "FJPG", 4) != 0) return JDR_FMT1;
  *w = d[4] | (d[5] << 8); *h = d[6] | (d[7] << 8); return JDR_OK;
}
JRESULT TJpgDecoder::drawJpg(int32_t, int32_t, const uint8_t* d, uint32_t n) {
  if (n < 13 || memcmp(d, "FJPG", 4) != 0) return JDR_FMT1;
  ++hooks::decodeCalls;
  int dl = hooks::decodeDelayMs.load(); if (dl > 0) usleep(dl * 1000);
  const int W = d[4] | (d[5] << 8), H = d[6] | (d[7] << 8);
  const uint32_t seed = d[8] | (d[9] << 8) | (d[10] << 16) | ((uint32_t)d[11] << 24);
  const int mode = d[12], s = scale_ ? scale_ : 1;
  const int dw = std::max(1, W / s), dh = std::max(1, H / s);
  JRESULT res = JDR_OK; uint16_t blk[16 * 16];
  for (int by = 0; by < (dh + 15) / 16 && res == JDR_OK; ++by)
    for (int bx = 0; bx < (dw + 15) / 16; ++bx) {
      if (mode == 1 && by * 2 >= (dh + 15) / 16) { res = JDR_INP; break; }
      for (int j = 0; j < 16; ++j) for (int i = 0; i < 16; ++i) {
        const int sx = (bx * 16 + i) * s, sy = (by * 16 + j) * s;
        blk[j * 16 + i] = (sx < W && sy < H) ? fakeJpgPixel(seed, sx, sy, W, H) : 0xFFFF;
      }
      if (!cb_(bx * 16, by * 16, 16, 16, blk)) { res = JDR_INP; break; }
    }
  return res;
}

// ---------------- icon / keypad ----------------
#define ICON(n) static uint16_t n##_px[64 * 64]; const lv_img_dsc_t n = {{LV_IMG_CF_TRUE_COLOR, 0, 0, 64, 64}, 64 * 64 * 2, (const uint8_t*)n##_px};
ICON(icon_t0_music) ICON(icon_t0_pet) ICON(icon_t0_calendar) ICON(icon_t0_visual) ICON(icon_t0_album) ICON(icon_t0_settings)
namespace Icons {
static lv_img_dsc_t g_icons[kThemeCount][(int)Id::COUNT];
const lv_img_dsc_t* get(Id id, uint8_t theme) {
  if ((int)id < 0 || id >= Id::COUNT) return nullptr;
  if (theme >= kThemeCount) theme = 0;
  lv_img_dsc_t& d = g_icons[theme][(int)id];
  if (!d.data) { d = icon_t0_music; }   // du lieu giong nhau; con tro khac nhau theo (theme, id)
  return &d;
}
}
namespace KeypadMatrix { void init() {} uint32_t poll(uint32_t) { return 0; } bool isIdle() { return true; } }

// ---------------- LvglPort / LvglLock ----------------
static pthread_mutex_t g_lvm = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
LvglLock::LvglLock() { pthread_mutex_lock(&g_lvm); }
LvglLock::~LvglLock() { pthread_mutex_unlock(&g_lvm); }
static lv_disp_draw_buf_t g_drawBuf;
static lv_disp_drv_t g_dispDrv;
static uint16_t* g_fb = nullptr;       // khung hinh day du, de test doc pixel
uint16_t* testFramebuffer() { return g_fb; }
static void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* c) {
  ++hooks::flushCount;
  if (a->x1 < 0 || a->y1 < 0 || a->x2 >= UI_W || a->y2 >= UI_H || a->x2 < a->x1 || a->y2 < a->y1) ++hooks::flushBadArea;
  else for (int y = a->y1; y <= a->y2; ++y) for (int x = a->x1; x <= a->x2; ++x) g_fb[y * UI_W + x] = c[(y - a->y1) * (a->x2 - a->x1 + 1) + (x - a->x1)].full;
  lv_disp_flush_ready(drv);
}
namespace LvglPort {
bool init() {
  lv_init();
  g_fb = (uint16_t*)calloc(UI_W * UI_H, 2);
  static lv_color_t* buf = (lv_color_t*)calloc(UI_W * LVGL_BUF_LINES, sizeof(lv_color_t));
  lv_disp_draw_buf_init(&g_drawBuf, buf, nullptr, UI_W * LVGL_BUF_LINES);
  lv_disp_drv_init(&g_dispDrv);
  g_dispDrv.hor_res = UI_W; g_dispDrv.ver_res = UI_H; g_dispDrv.flush_cb = flushCb; g_dispDrv.draw_buf = &g_drawBuf;
  lv_disp_drv_register(&g_dispDrv);
  return true;
}
void tick() { LvglLock l; lv_timer_handler(); }
}
