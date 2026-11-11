#pragma once
// Bản sao struct Theme (ARCHITECTURE.md mục 6.11) cho test máy tính. Themes::current() do test định nghĩa.
#include <lvgl.h>
#include <cstdint>

struct Theme {
    const char* key;
    const char* name;
    uint32_t bg, panel, accent, text, textOnPanel, focus, icon, scrim;
    uint8_t scrimOpa;
    const lv_img_dsc_t* bgImage;
};

namespace Themes {
const Theme& current();
}
