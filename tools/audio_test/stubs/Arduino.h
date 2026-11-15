#pragma once
// STUB Arduino cho test may tinh cua audio_service.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

uint32_t millis();

class SerialStub {
 public:
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
  }
  void println(const char* s) { ::puts(s); }
  void print(const char* s) { ::fputs(s, stdout); }
};
extern SerialStub Serial;
