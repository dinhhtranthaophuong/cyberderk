#pragma once
// Bo icon app cho Main Screen, 3 theme x 6 icon, moi icon 64x64 (man 320x240).
// Du lieu nam o icons_t0/1/2.cpp (sinh tu dong tu anh PNG, luu trong flash, khong ton RAM).
#include <lvgl.h>

#include <cstdint>

#include "assets/icons/icons_gen.hpp"

namespace Icons {
constexpr uint8_t kThemeCount = 3;  // 0 = dark blue, 1 = mint, 2 = pink

enum class Id : uint8_t { MUSIC, PET, VISUAL, CALENDAR, ALBUM, SETTINGS, COUNT };

constexpr int kSizePx = 64;

// Tra ve nullptr neu id khong hop le. Theme ngoai khoang -> dung theme 0.
const lv_img_dsc_t* get(Id id, uint8_t theme);
}  // namespace Icons
