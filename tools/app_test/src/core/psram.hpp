#pragma once
#include <cstddef>

namespace Psram {
// Cap phat trong PSRAM. Tra nullptr neu het bo nho.
void* alloc(size_t bytes);
// Giai phong. An toan voi nullptr.
void release(void* p);
// Dung luong PSRAM con trong (byte).
size_t freeBytes();
}  // namespace Psram
