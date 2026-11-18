#pragma once
// Stub (header that khong duoc gui): chi cac truong ma Main Screen dung.
#include <stdint.h>
struct DateTime { uint16_t year; uint8_t month, day, hour, minute, second; };
namespace TimeService { DateTime now(); }
