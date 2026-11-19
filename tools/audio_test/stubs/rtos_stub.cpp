// STUB FreeRTOS / I2S / PSRAM / Arduino cho test may tinh cua audio_service.
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>

#include "Arduino.h"
#include "core/psram.hpp"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "test_hooks.hpp"

SerialStub Serial;

namespace hooks {
double timeScale = 5.0;
std::atomic<int> lockOrderViolations(0);
std::atomic<int> relockErrors(0);
std::atomic<long> psramOutstanding(0);
std::atomic<long> psramAllocs(0);
std::atomic<int> psramFailNth(0);
std::atomic<int> i2sInstallFail(0);
std::atomic<int> i2sInstallCalls(0);
std::atomic<int> i2sPinFail(0);
std::atomic<int> i2sUninstallCalls(0);
std::atomic<int> i2sZeroCalls(0);
i2s_config_t i2sCfg;
i2s_pin_config_t i2sPins;
std::atomic<long> captureLRMismatch(0);
}  // namespace hooks

static std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();
uint32_t millis() {
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_t0).count();
  return (uint32_t)(ms * hooks::timeScale);
}

static void sleepUs(double us) {
  if (us < 1) us = 1;
  struct timespec ts;
  ts.tv_sec = (time_t)(us / 1e6);
  ts.tv_nsec = (long)((us - ts.tv_sec * 1e6) * 1000.0);
  nanosleep(&ts, nullptr);
}

// ---------------- Khoa co kiem tra thu tu ----------------
struct SemStub {
  pthread_mutex_t m;
  int rank;
  bool recursive;
  const char* name;
};
static thread_local std::vector<SemStub*> t_held;
static std::mutex g_msgMtx;
static std::string g_firstMsg;
static std::atomic<int> g_rankCounter(0);

static void noteMsg(const std::string& s) {
  std::lock_guard<std::mutex> g(g_msgMtx);
  if (g_firstMsg.empty()) g_firstMsg = s;
}
std::string hooks::firstLockMessage() {
  std::lock_guard<std::mutex> g(g_msgMtx);
  return g_firstMsg;
}

SemaphoreHandle_t stubMutexCreate(int rank, bool recursive, const char* name) {
  SemStub* s = new SemStub();
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&s->m, &a);
  pthread_mutexattr_destroy(&a);
  s->rank = rank;
  s->recursive = recursive;
  s->name = name;
  return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
  int r = ++g_rankCounter;   // thu tu tao = hang: gCtlMtx=1, gRingMtx=2
  static const char* names[] = {"?", "gCtlMtx", "gRingMtx", "mtx3", "mtx4"};
  return stubMutexCreate(r, false, names[r < 5 ? r : 4]);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks) {
  if (!h) { noteMsg("Take(nullptr)"); hooks::relockErrors++; return pdFALSE; }
  bool already = std::find(t_held.begin(), t_held.end(), h) != t_held.end();
  if (already && !h->recursive) {
    hooks::relockErrors++;
    noteMsg(std::string("khoa lai ") + h->name + " khi dang giu (se treo that)");
    return pdFALSE;
  }
  if (!already) {
    for (size_t i = 0; i < t_held.size(); i++) {
      if (t_held[i]->rank >= h->rank) {
        hooks::lockOrderViolations++;
        noteMsg(std::string("lay ") + h->name + " (hang " + std::to_string(h->rank) + ") khi dang giu " +
                t_held[i]->name + " (hang " + std::to_string(t_held[i]->rank) + ")");
      }
    }
  }
  if (ticks == portMAX_DELAY) {
    pthread_mutex_lock(&h->m);
  } else {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ticks / 1000;
    ts.tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    if (pthread_mutex_timedlock(&h->m, &ts) != 0) return pdFALSE;
  }
  t_held.push_back(h);
  return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  if (!h) return pdFALSE;
  for (size_t i = t_held.size(); i-- > 0;) {
    if (t_held[i] == h) { t_held.erase(t_held.begin() + i); break; }
  }
  return pthread_mutex_unlock(&h->m) == 0 ? pdTRUE : pdFALSE;
}

// ---------------- Task ----------------
static std::mutex g_taskMtx;
static std::vector<hooks::TaskRec> g_tasks;
struct Thunk { TaskFunction_t fn; void* arg; };
static void* thunkMain(void* p) {
  Thunk t = *(Thunk*)p;
  delete (Thunk*)p;
  t.fn(t.arg);
  return nullptr;
}
std::vector<hooks::TaskRec> hooks::tasks() {
  std::lock_guard<std::mutex> g(g_taskMtx);
  return g_tasks;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* out, BaseType_t core) {
  {
    std::lock_guard<std::mutex> g(g_taskMtx);
    hooks::TaskRec r; r.name = name; r.stack = stack; r.prio = (int)prio; r.core = core;
    g_tasks.push_back(r);
  }
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 1 << 20);
  Thunk* t = new Thunk{fn, arg};
  int rc = pthread_create(&th, &at, thunkMain, t);
  pthread_attr_destroy(&at);
  if (out) *out = nullptr;
  return rc == 0 ? pdTRUE : pdFALSE;
}
void vTaskDelay(TickType_t ticks) { sleepUs((double)ticks * 1000.0 / hooks::timeScale); }

// ---------------- PSRAM ----------------
namespace {
std::atomic<int> g_psramCount(0);
struct Hdr { size_t size; size_t pad; };
}
void hooks::psramResetCounter() { g_psramCount = 0; }
namespace Psram {
void* alloc(size_t bytes) {
  int n = ++g_psramCount;
  int f = hooks::psramFailNth.load();
  if (f > 0 && n == f) return nullptr;
  Hdr* h = (Hdr*)malloc(sizeof(Hdr) + bytes);
  if (!h) return nullptr;
  h->size = bytes;
  memset(h + 1, 0xCD, bytes);
  hooks::psramOutstanding += (long)bytes;
  hooks::psramAllocs++;
  return h + 1;
}
void release(void* p) {
  if (!p) return;
  Hdr* h = (Hdr*)p - 1;
  hooks::psramOutstanding -= (long)h->size;
  free(h);
}
size_t freeBytes() { return 8u << 20; }
}  // namespace Psram

// ---------------- I2S ----------------
static std::mutex g_capMtx;
static std::vector<int16_t> g_capL;
static std::vector<uint32_t> g_rates;
static std::atomic<uint32_t> g_curRate(44100);
static bool g_i2sInstalled = false;

std::vector<uint32_t> hooks::i2sRateChanges() {
  std::lock_guard<std::mutex> g(g_capMtx);
  return g_rates;
}
void hooks::captureClear() {
  std::lock_guard<std::mutex> g(g_capMtx);
  g_capL.clear();
  g_rates.clear();
  captureLRMismatch = 0;
}
size_t hooks::captureFrames() {
  std::lock_guard<std::mutex> g(g_capMtx);
  return g_capL.size();
}
std::vector<int16_t> hooks::captureLeft(size_t from) {
  std::lock_guard<std::mutex> g(g_capMtx);
  if (from >= g_capL.size()) return std::vector<int16_t>();
  return std::vector<int16_t>(g_capL.begin() + from, g_capL.end());
}

esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t* c, int, void*) {
  hooks::i2sInstallCalls++;
  if (hooks::i2sInstallFail.load()) return ESP_FAIL;
  if (g_i2sInstalled) return ESP_FAIL;   // driver that: cai lan hai bao loi
  g_i2sInstalled = true;
  hooks::i2sCfg = *c;
  g_curRate = c->sample_rate;
  return ESP_OK;
}
esp_err_t i2s_driver_uninstall(i2s_port_t) {
  hooks::i2sUninstallCalls++;
  if (!g_i2sInstalled) return ESP_FAIL;
  g_i2sInstalled = false;
  return ESP_OK;
}
esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t* p) {
  if (hooks::i2sPinFail.load()) return ESP_FAIL;
  hooks::i2sPins = *p;
  return ESP_OK;
}
esp_err_t i2s_zero_dma_buffer(i2s_port_t) { hooks::i2sZeroCalls++; return ESP_OK; }
esp_err_t i2s_set_clk(i2s_port_t, uint32_t rate, i2s_bits_per_sample_t, i2s_channel_t) {
  std::lock_guard<std::mutex> g(g_capMtx);
  g_rates.push_back(rate);
  g_curRate = rate;
  return ESP_OK;
}
esp_err_t i2s_write(i2s_port_t, const void* src, size_t bytes, size_t* written, TickType_t) {
  const int16_t* s = (const int16_t*)src;
  size_t frames = bytes / 4;
  {
    std::lock_guard<std::mutex> g(g_capMtx);
    for (size_t i = 0; i < frames; i++) {
      g_capL.push_back(s[2 * i]);
      if (s[2 * i] != s[2 * i + 1]) hooks::captureLRMismatch++;
    }
  }
  // Mo phong DMA: ghi chan theo toc do phat (nhanh hon timeScale lan)
  sleepUs((double)frames * 1e6 / (double)g_curRate.load() / hooks::timeScale);
  if (written) *written = bytes;
  return ESP_OK;
}
