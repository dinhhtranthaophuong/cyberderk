#pragma once
// Bản sao ARCHITECTURE.md mục 3.2. SCREEN_W/H lấy từ -D khi build (mặc định 320x240 như board).
#ifndef SCREEN_W
#define SCREEN_W 320
#endif
#ifndef SCREEN_H
#define SCREEN_H 240
#endif
constexpr int UI_W = SCREEN_W;
constexpr int UI_H = SCREEN_H;
constexpr bool UI_COMPACT = (SCREEN_W < 240);
