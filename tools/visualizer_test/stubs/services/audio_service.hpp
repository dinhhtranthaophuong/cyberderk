#pragma once
// Chữ ký đúng ARCHITECTURE.md mục 6.7 (phần Visualizer dùng). Test tự định nghĩa.
#include <cstddef>
#include <cstdint>

enum class AudioState : uint8_t { STOPPED, PLAYING, PAUSED, ERROR };

namespace AudioService {
AudioState state();
size_t readPcmTap(int16_t* dst, size_t n);
}
