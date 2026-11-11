#pragma once
#include <cstdint>

#include "core/app_base.hpp"

namespace AppManager {
// Goi SAU LvglPort::init() va InputService::init(). Tao app dau tien (MAIN_SCREEN).
void init();

// TRI HOAN: chi dat co, viec chuyen app that su xay ra o lan tick() ke tiep.
// Nho do goi tu event callback cua LVGL la an toan (khong xoa object dang xu ly event).
// Goi toi chinh app dang chay thi bo qua. Thread-safe.
void goTo(AppId id);
void goHome();  // = goTo(MAIN_SCREEN)

// TRI HOAN nhu goTo: dung lai man hinh cua app hien tai (vd sau khi doi theme) de ap mau moi.
// App moi duoc tao lai tu dau; trang thai nao can giu thi app tu giu (xem main_screen).
void reload();

AppId current();

// Goi deu dan tu loop() (Core 1). Tu lay LvglLock ben trong.
void tick(uint32_t nowMs);

// Day phim custom (KEY_HOME, KEY_OPTION) vao hang doi. Thread-safe, khong chan.
// InputService goi ham nay; app KHONG can goi.
void postKey(uint32_t key);
}  // namespace AppManager
