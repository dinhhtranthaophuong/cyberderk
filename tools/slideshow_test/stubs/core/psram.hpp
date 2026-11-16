#pragma once
#include <cstddef>
namespace Psram {
void* alloc(size_t bytes);
void release(void* p);
size_t freeBytes();
}
