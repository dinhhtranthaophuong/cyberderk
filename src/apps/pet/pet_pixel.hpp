#pragma once
// Tiện ích điểm ảnh chung cho PetSprite, PetIcons, PetDisc.
// Định dạng: LV_IMG_CF_TRUE_COLOR_ALPHA, mỗi điểm = [lv_color_t][1 byte alpha].

#include <lvgl.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

static_assert(sizeof(lv_color_t) + 1 == LV_IMG_PX_SIZE_ALPHA_BYTE,
              "Dinh dang pixel LVGL khong khop: kiem tra LV_COLOR_DEPTH trong lv_conf.h");

constexpr int kPetBytesPerPx = LV_IMG_PX_SIZE_ALPHA_BYTE;

inline void petPutPixel(uint8_t* out, uint32_t rgb, uint8_t alpha) {
    const lv_color_t c = lv_color_hex(rgb);
    std::memcpy(out, &c, sizeof(lv_color_t));
    out[sizeof(lv_color_t)] = alpha;
}

inline void petPutTransparent(uint8_t* out) {
    std::memset(out, 0, kPetBytesPerPx);
}

// Điền mô tả ảnh (lv_img_dsc_t) trỏ tới buffer w x h điểm đã có sẵn.
inline void petInitDsc(lv_img_dsc_t* d, const uint8_t* data, int w, int h) {
    *d = lv_img_dsc_t{};
    d->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    d->header.always_zero = 0;
    d->header.w = w;
    d->header.h = h;
    d->data_size = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * kPetBytesPerPx;
    d->data = data;
}

// Vẽ lưới grid x grid ô vào buffer (grid*scale)^2 điểm: mỗi ô thành khối scale x scale,
// đúng số nguyên lần nên pixel art luôn sắc nét. colorAt(gx, gy, &rgb) trả false nếu
// ô trong suốt.
template <typename ColorFn>
inline void petRenderGrid(uint8_t* dst, int grid, int scale, ColorFn colorAt) {
    const size_t rowBytes = static_cast<size_t>(grid) * scale * kPetBytesPerPx;
    uint8_t* p = dst;
    for (int gy = 0; gy < grid; ++gy) {
        uint8_t* first = p;
        for (int gx = 0; gx < grid; ++gx) {
            uint8_t px[kPetBytesPerPx];
            uint32_t rgb = 0;
            if (colorAt(gx, gy, &rgb)) {
                petPutPixel(px, rgb, 0xFF);
            } else {
                petPutTransparent(px);
            }
            for (int sx = 0; sx < scale; ++sx) {
                std::memcpy(p, px, kPetBytesPerPx);
                p += kPetBytesPerPx;
            }
        }
        for (int sy = 1; sy < scale; ++sy) {   // các hàng còn lại của ô: chép từ hàng đầu
            std::memcpy(p, first, rowBytes);
            p += rowBytes;
        }
    }
}
