#pragma once
// Stub Arduino cho test B3. Dong ho ao: test tu quyet dinh millis().
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <deque>
#include <mutex>
extern "C" {
#endif
uint32_t millis(void);
#ifdef __cplusplus
}
struct SerialStub {
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (!quiet) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
  }
  void println(const char* s) { if (!quiet) puts(s); }
  int available();
  int read();
  void feed(char c);                 // test: gia lap go phim tren Serial Monitor
  bool quiet = true;
 private:
  std::mutex m_;
  std::deque<int> q_;
};
extern SerialStub Serial;
struct EspStub { uint32_t getFreeHeap() { return 300000; } };
extern EspStub ESP;
extern std::atomic<int> g_delayCalls;
inline void delay(uint32_t) { ++g_delayCalls; }
#endif
