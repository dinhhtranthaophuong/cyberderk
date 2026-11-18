#pragma once
// Các kiểu dùng chung của app Pet. Không phụ thuộc LVGL hay Arduino.

#include <cstdint>

// Biểu cảm của mèo.
enum class PetMood : uint8_t { NORMAL, HAPPY, SLEEPING, SAD, COUNT };

// Màu lông. Thứ tự là thứ tự xoay vòng khi bấm nút Skin.
enum class PetSkin : uint8_t { CALICO, TUXEDO, WHITE, ORANGE_TABBY, COUNT };

// Chỉ số của thú (100 là tốt nhất). ENERGY là vòng của nút Sleep: giảm dần khi thức,
// hết một vòng (về 0) thì mèo đi ngủ và trời tối; khi ngủ thì hồi dần về 100 rồi dậy.
enum class PetStat : uint8_t { HUNGER, FUN, CLEAN, ENERGY, COUNT };

// Icon pixel trên các nút.
enum class PetIcon : uint8_t { EAT, PLAY, BATH, SLEEP, COUNT };
