#pragma once
// Quy uoc phim 3x3 (xem ARCHITECTURE.md muc 4).
//
//   1 = BACK(ESC)  2 = UP      3 = OPTION
//   4 = LEFT       5 = ENTER   6 = RIGHT
//   7 = PREV       8 = DOWN    9 = NEXT
//   Nhan nhanh phim 1 = ESC (gui khi tha phim). Giu phim 1 >= 800 ms = HOME (ve MAIN_SCREEN).
//
// Cac phim dieu huong dung ma LV_KEY_* cua LVGL. Chi hai phim custom duoi day.
#include <lvgl.h>

#include <cstdint>

constexpr uint32_t KEY_OPTION = 0x2001;
constexpr uint32_t KEY_HOME = 0x2002;
constexpr uint32_t KEY_HOME_HOLD_MS = 800;

// So phim tren ban phim (1..9, dem theo hang tu tren xuong, trai sang phai) -> ma phim.
// Dung chung cho ban phim that va cho Serial Monitor. Tra 0 neu khong hop le.
inline uint32_t keyForNumber(int n) {
  switch (n) {
    case 1: return LV_KEY_ESC;
    case 2: return LV_KEY_UP;
    case 3: return KEY_OPTION;
    case 4: return LV_KEY_LEFT;
    case 5: return LV_KEY_ENTER;
    case 6: return LV_KEY_RIGHT;
    case 7: return LV_KEY_PREV;
    case 8: return LV_KEY_DOWN;
    case 9: return LV_KEY_NEXT;
    default: return 0;
  }
}
