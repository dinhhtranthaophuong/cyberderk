#pragma once
// Kich thuoc man hinh den tu build_flags (SCREEN_W / SCREEN_H), xem platformio.ini.
// Moi UI phai dung cac hang so nay, khong ghi cung so pixel.

#if !defined(SCREEN_W) || !defined(SCREEN_H)
#error "Can dinh nghia SCREEN_W va SCREEN_H trong build_flags cua env"
#endif

constexpr int  UI_W = SCREEN_W;
constexpr int  UI_H = SCREEN_H;
constexpr bool UI_COMPACT = (SCREEN_W < 240);       // true voi 160x128
constexpr int  GRID_COLS = 2;
constexpr int  GRID_ROWS = 3;                        // luoi app 2 cot x 3 hang (theo mockup)
constexpr int  ICON_SIZE = UI_COMPACT ? 28 : 56;
constexpr int  LVGL_BUF_LINES = UI_H / 10;           // buffer LVGL = 1/10 man hinh
