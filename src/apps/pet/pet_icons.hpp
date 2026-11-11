#pragma once
// PetIcons: 4 icon pixel (cá, cuộn len, giọt nước, Zz) cho các nút của app Pet.
//
// Hình vẽ nằm trong pet_art.cpp (lưới 10x10). Ở đây vẽ cả 4 icon vào MỘT khối PSRAM
// (RGB565+alpha), phóng đúng số nguyên lần: x2 (20 px) trên màn 160x128, x4 trên 2.8".
// Chỉ gọi từ ngữ cảnh UI, không gọi từ Core 0.

#include <lvgl.h>
#include <cstdint>
#include "config/display.hpp"
#include "apps/pet/pet_types.hpp"

class PetIcons {
public:
    static constexpr int kGrid  = 10;
    static constexpr int kScale = UI_COMPACT ? 2 : 4;
    static constexpr int kSize  = kGrid * kScale;                    // cạnh một icon, px
    static constexpr int kCount = static_cast<int>(PetIcon::COUNT);

    PetIcons() = default;
    ~PetIcons();   // chỉ giải phóng buffer; không đụng LVGL
    PetIcons(const PetIcons&) = delete;
    PetIcons& operator=(const PetIcons&) = delete;

    // Cấp buffer PSRAM và vẽ 4 icon. Trả false nếu hết PSRAM (get() khi đó trả nullptr).
    bool create();

    // Giải phóng buffer. Gọi SAU khi các lv_img dùng icon đã bị xóa (hoặc cùng lúc xóa screen).
    void destroy();

    // Mô tả ảnh của icon (địa chỉ ổn định trong suốt đời đối tượng); nullptr nếu chưa create.
    const lv_img_dsc_t* get(PetIcon icon) const;

private:
    uint8_t*     buf_ = nullptr;
    lv_img_dsc_t dsc_[kCount] = {};
};
