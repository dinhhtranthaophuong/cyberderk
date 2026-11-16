#pragma once
// Stub Arduino cho test tren may tinh.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <atomic>
struct SerialStub {
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (!quiet) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
  }
  void println(const char* s) { if (!quiet) puts(s); }
  bool quiet = true;
};
extern SerialStub Serial;
extern std::atomic<int> g_delayCalls;   // code dich vu khong duoc goi delay()
inline void delay(uint32_t) { ++g_delayCalls; }
