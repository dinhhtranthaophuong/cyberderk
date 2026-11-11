#pragma once
#include <cstdint>

struct Image {
    uint16_t* pixels = nullptr;   // RGB565 trong PSRAM
    int w = 0, h = 0;
};

namespace ImageService {
// Giải mã JPG từ SD, thu nhỏ để vừa trong targetW x targetH (giữ tỉ lệ).
// Dùng scale 1/2, 1/4, 1/8 của bộ giải mã khi có thể.
// Chạy được ở task Core 0; KHÔNG chạm LVGL.
bool decode(const char* path, int targetW, int targetH, Image* out);
void free(Image* img);        // release pixels, reset về rỗng
}
