#pragma once
// Bộ giả lập phần cứng cho test máy tính của time_service.cpp (không phải code firmware).
// Toàn bộ stub nằm trong header này; các header Arduino.h, Wire.h... chỉ include lại file này.
// Đồng hồ giả: millis() do test điều khiển, giờ hệ thống = mốc + (millis - mốc)/1000.
// settimeofday()/time() bị thay bằng bản giả để KHÔNG đụng đồng hồ thật của máy chạy test.

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/time.h>

namespace sim {

inline std::atomic<uint32_t> millisNow{100000};
inline std::atomic<bool> autoAdvance{false};   // true: mỗi lần gọi millis() tự +1 ms (test đa luồng)

inline std::mutex clockMu;
inline time_t baseEpoch = 0;
inline uint32_t baseMillis = 0;

inline std::mutex logMu;
inline std::vector<std::string> log;

inline bool wifiConnected = false;

// SNTP giả: sntpPollsLeft = số lần hỏi còn "đang chạy" trước khi hoàn tất; -1 = không bao giờ xong.
inline int sntpStatus = 0;                     // 0 RESET, 1 COMPLETED, 2 IN_PROGRESS
inline int sntpPollsLeft = -1;
inline time_t ntpEpoch = 0;
inline int sntpStopCalls = 0;
inline int configCalls = 0;
inline std::string lastTz;
inline bool resetBeforeConfig = false;

}  // namespace sim

// ---------- thời gian ----------
inline uint32_t millis() {
  if (sim::autoAdvance.load()) return sim::millisNow.fetch_add(1) + 1;
  return sim::millisNow.load();
}

inline time_t fake_time(time_t* out) {
  std::lock_guard<std::mutex> g(sim::clockMu);
  uint32_t m = sim::millisNow.load();
  time_t t = sim::baseEpoch + static_cast<time_t>((m - sim::baseMillis) / 1000);
  if (out) *out = t;
  return t;
}

inline int fake_settimeofday(const timeval* tv, const void*) {
  std::lock_guard<std::mutex> g(sim::clockMu);
  sim::baseEpoch = tv->tv_sec;
  sim::baseMillis = sim::millisNow.load();
  return 0;
}

namespace sim {
inline void setSysEpoch(time_t e) {
  timeval tv{};
  tv.tv_sec = e;
  fake_settimeofday(&tv, nullptr);
}
inline void reset() {
  millisNow = 100000;
  autoAdvance = false;
  setSysEpoch(0);   // chưa có RTC/NTP: giờ hệ thống bắt đầu từ epoch 0 giống board
  { std::lock_guard<std::mutex> g(logMu); log.clear(); }
  wifiConnected = false;
  sntpStatus = 0;
  sntpPollsLeft = -1;
  ntpEpoch = 0;
  sntpStopCalls = 0;
  configCalls = 0;
  lastTz.clear();
  resetBeforeConfig = false;
}
}  // namespace sim

// Phải đứng SAU mọi #include chuẩn ở trên.
#define time fake_time
#define settimeofday fake_settimeofday

// ---------- Serial ----------
struct SerialStub {
  void push(const char* s) {
    std::lock_guard<std::mutex> g(sim::logMu);
    sim::log.emplace_back(s);
    if (std::getenv("TIME_TEST_VERBOSE")) std::printf("  [serial] %s\n", s);
  }
  void println(const char* s) { push(s); }
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    push(buf);
  }
};
inline SerialStub Serial;

// ---------- FreeRTOS ----------
typedef void* SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

// Dùng try_lock + vòng chờ thay vì try_lock_for: ThreadSanitizer không theo dõi pthread_mutex_clocklock
// mà recursive_timed_mutex dùng, nên sẽ báo sai "unlock khi chưa lock".
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new std::recursive_mutex; }
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t ticks) {
  auto* m = static_cast<std::recursive_mutex*>(h);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ticks);
  while (!m->try_lock()) {
    if (std::chrono::steady_clock::now() >= deadline) return pdFALSE;
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  return pdTRUE;
}
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) {
  static_cast<std::recursive_mutex*>(h)->unlock();
  return pdTRUE;
}
inline void vTaskDelay(TickType_t ticks) { sim::millisNow.fetch_add(ticks); }   // 1 tick = 1 ms, không ngủ thật

// ---------- WiFi ----------
enum wl_status_t { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
struct WiFiStub {
  wl_status_t status() const { return sim::wifiConnected ? WL_CONNECTED : WL_DISCONNECTED; }
};
inline WiFiStub WiFi;

// ---------- SNTP ----------
enum sntp_sync_status_t {
  SNTP_SYNC_STATUS_RESET = 0,
  SNTP_SYNC_STATUS_COMPLETED = 1,
  SNTP_SYNC_STATUS_IN_PROGRESS = 2
};
inline void sntp_set_sync_status(sntp_sync_status_t s) { sim::sntpStatus = static_cast<int>(s); }
inline sntp_sync_status_t sntp_get_sync_status() {
  if (sim::sntpStatus == SNTP_SYNC_STATUS_COMPLETED) return SNTP_SYNC_STATUS_COMPLETED;
  if (sim::sntpPollsLeft >= 0) {
    if (sim::sntpPollsLeft == 0) {
      sim::setSysEpoch(sim::ntpEpoch);   // NTP chỉnh giờ hệ thống rồi báo xong
      sim::sntpStatus = SNTP_SYNC_STATUS_COMPLETED;
      return SNTP_SYNC_STATUS_COMPLETED;
    }
    sim::sntpPollsLeft--;
  }
  return SNTP_SYNC_STATUS_IN_PROGRESS;
}
inline void sntp_stop() { sim::sntpStopCalls++; }
inline void configTzTime(const char* tz, const char*, const char* = nullptr, const char* = nullptr) {
  sim::configCalls++;
  sim::lastTz = tz;
  sim::resetBeforeConfig = (sim::sntpStatus == SNTP_SYNC_STATUS_RESET);
  setenv("TZ", tz, 1);
  tzset();
}

// ---------- Wire + DS3231 giả ----------
// Mô hình chip: 0x13 thanh ghi, con trỏ tự tăng và quay vòng. Phát hiện hai giao dịch chồng nhau
// (violations), vì một TwoWire thật không an toàn khi hai task dùng cùng lúc.
class TwoWire {
 public:
  bool chipPresent = true;
  int failTxNext = 0;      // endTransmission tiếp theo trả lỗi 4
  int failReadNext = 0;    // requestFrom tiếp theo trả 0 byte
  uint8_t regs[0x13] = {};

  int beginCalls = 0, lastSda = -1, lastScl = -1;
  uint32_t lastFreq = 0;
  uint16_t timeoutMs = 0;
  int transactions = 0;    // số lần endTransmission
  int reads = 0;           // số lần requestFrom
  int writes = 0;          // số giao dịch có ghi dữ liệu vào thanh ghi
  std::atomic<int> active{0};
  std::atomic<int> violations{0};

  void reset() {
    chipPresent = true;
    failTxNext = failReadNext = 0;
    for (auto& r : regs) r = 0;
    beginCalls = 0; lastSda = lastScl = -1; lastFreq = 0; timeoutMs = 0;
    transactions = reads = writes = 0;
    active = 0; violations = 0;
    addr_ = 0; ptr_ = 0; tx_.clear(); rx_.clear();
  }

  bool begin(int sda, int scl, uint32_t freq = 0) {
    beginCalls++; lastSda = sda; lastScl = scl; lastFreq = freq;
    return true;
  }
  void setTimeOut(uint16_t ms) { timeoutMs = ms; }

  void beginTransmission(uint8_t a) {
    if (active.fetch_add(1) > 0) violations++;
    addr_ = a;
    tx_.clear();
  }
  size_t write(uint8_t b) { tx_.push_back(b); return 1; }
  size_t write(const uint8_t* d, size_t n) { for (size_t i = 0; i < n; i++) tx_.push_back(d[i]); return n; }

  uint8_t endTransmission(bool = true) {
    transactions++;
    uint8_t rc = 0;
    if (!chipPresent || addr_ != 0x68) {
      rc = 2;
    } else if (failTxNext > 0) {
      failTxNext--;
      rc = 4;
    } else if (!tx_.empty()) {
      ptr_ = static_cast<uint8_t>(tx_[0] % 0x13);
      if (tx_.size() > 1) {
        writes++;
        for (size_t i = 1; i < tx_.size(); i++) {
          regs[ptr_] = tx_[i];
          ptr_ = static_cast<uint8_t>((ptr_ + 1) % 0x13);
        }
      }
    }
    tx_.clear();
    active.fetch_sub(1);
    return rc;
  }

  uint8_t requestFrom(uint8_t a, uint8_t n) {
    if (active.fetch_add(1) > 0) violations++;
    reads++;
    rx_.clear();
    if (!chipPresent || a != 0x68) { active.fetch_sub(1); return 0; }
    if (failReadNext > 0) { failReadNext--; active.fetch_sub(1); return 0; }
    for (uint8_t i = 0; i < n; i++) {
      rx_.push_back(regs[ptr_]);
      ptr_ = static_cast<uint8_t>((ptr_ + 1) % 0x13);
    }
    return n;   // giao dịch đọc coi là xong khi byte cuối được read()
  }
  int read() {
    if (rx_.empty()) return -1;
    uint8_t v = rx_.front();
    rx_.pop_front();
    if (rx_.empty()) active.fetch_sub(1);
    return v;
  }

 private:
  uint8_t addr_ = 0;
  uint8_t ptr_ = 0;
  std::vector<uint8_t> tx_;
  std::deque<uint8_t> rx_;
};
inline TwoWire Wire;
