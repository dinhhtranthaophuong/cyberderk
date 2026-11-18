#include "psram.hpp"

#include <esp_heap_caps.h>

namespace Psram {

void* alloc(size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void release(void* p) {
  if (p) heap_caps_free(p);
}

size_t freeBytes() {
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

}  // namespace Psram
