#pragma once
// Dữ liệu hình ảnh pixel của app Pet: mèo (lưới 20x20) và 4 icon (lưới 10x10).
//
// File này THUẦN DỮ LIỆU: không LVGL, không Arduino, nên chạy thử và xem trước được
// trên máy tính. Phần cấp buffer và hiển thị nằm ở PetSprite / PetIcons.

#include <cstdint>
#include "apps/pet/pet_types.hpp"

constexpr int kPetGrid     = 20;   // lưới mèo 20x20
constexpr int kPetIconGrid = 10;   // lưới icon 10x10

// Hiệu ứng phản ứng khi bấm nút (vẽ vào ô trống quanh mèo).
enum class PetFx : uint8_t { NONE, EAT, PLAY, BATH };

// Trạng thái hoạt ảnh tại một thời điểm (do PetSprite::tick tính ra).
struct PetAnim {
    bool blink     = false;   // khép mắt (khi thức)
    bool earTwitch = false;   // tai phải cụp xuống (khi thức)
    bool tailUp    = false;   // đuôi nhấc lên 1 ô (khi thức)
    bool breathe   = false;   // phồng thân lên 1 hàng (khi ngủ)
        uint8_t tear   = 0;       // giọt nước mắt khi buồn: 0 ở mắt, 1 ở má, 2 biến mất
        PetFx   fx     = PetFx::NONE;
        uint8_t fxPhase = 0;      // nhịp của hiệu ứng (EAT/PLAY: 0..1, BATH: 0..5)
    bool operator==(const PetAnim& o) const {
        return blink == o.blink && earTwitch == o.earTwitch && tailUp == o.tailUp && breathe == o.breathe &&
               tear == o.tear && fx == o.fx && fxPhase == o.fxPhase; 
    }
    bool operator!=(const PetAnim& o) const { return !(*this == o); }
};

// Màu của ô (gx, gy) của mèo ứng với biểu cảm, màu lông và hoạt ảnh. Trả false nếu ô trong suốt.
bool petBodyColor(int gx, int gy, PetMood mood, PetSkin skin, uint32_t* rgb,
                  const PetAnim& anim = PetAnim{});

// Màu của ô (gx, gy) của icon. Trả false nếu ô trong suốt.
bool petIconColor(PetIcon icon, int gx, int gy, uint32_t* rgb);

// Màu hiển thị trên nút Skin: tam thể có 3 dải màu, các skin khác chỉ có 1.
int      petSkinBands(PetSkin skin);
uint32_t petSkinBandColor(PetSkin skin, int band);
