#pragma once
// Bo icon app cho Main Screen, 4 bo x 6 icon, moi icon 64x64 (man 320x240).
// Du lieu nam o icons_t0..t3.cpp (sinh tu dong tu anh PNG, luu trong flash, khong ton RAM).
#include <lvgl.h>

#include <cstdint>

#include "assets/icons/icons_gen.hpp"

namespace Icons {
constexpr uint8_t kThemeCount = 4;  // so bo icon: 0 = dark blue, 1 = mint, 2 = pink, 3 = Giang sinh

enum class Id : uint8_t { MUSIC, PET, VISUAL, CALENDAR, ALBUM, SETTINGS, COUNT };

constexpr int kSizePx = 64;

// Tra ve nullptr neu id khong hop le. Chi so bo icon ngoai khoang -> dung bo 0.
const lv_img_dsc_t* get(Id id, uint8_t theme);
}  // namespace Icons
