#include "apps/pet/pet_disc.hpp"

#include <Arduino.h>
#include <cmath>
#include "apps/pet/pet_pixel.hpp"
#include "core/psram.hpp"

namespace {
constexpr float    kRadToDeg  = 57.29577951f;
constexpr uint32_t kRingRgb   = 0xF5F5F5;   // viền sáng của ô màu skin
constexpr uint8_t  kMaskAlpha = 150;        // độ đậm của mặt nạ tối (đen, ~59%)
}  // namespace

PetDisc::~PetDisc() {
    // Không đụng LVGL ở đây: object (nếu còn) đã bị hủy cùng screen của app.
    Psram::release(buf_);
    buf_ = nullptr;
}

bool PetDisc::create(lv_obj_t* parent, int diameter) {
    destroy();
    if (parent == nullptr || diameter < 4) return false;

    const size_t bytes = static_cast<size_t>(diameter) * diameter * kPetBytesPerPx;
    buf_ = static_cast<uint8_t*>(Psram::alloc(bytes));
    if (buf_ == nullptr) {
        Serial.println("[PetDisc] khong du PSRAM");
        return false;
    }
    std::memset(buf_, 0, bytes);   // trong suốt hoàn toàn cho tới khi có show*()
    d_ = diameter;
    mode_ = Mode::NONE;

    petInitDsc(&dsc_, buf_, d_, d_);
    img_ = lv_img_create(parent);
    lv_obj_clear_flag(img_, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(img_, &dsc_);
    lv_obj_center(img_);
    return true;
}

void PetDisc::destroy() {
    // Xóa object trước, rồi mới giải phóng buffer mà ảnh đang trỏ tới.
    if (img_ != nullptr) {
        lv_obj_del(img_);
    }
    img_ = nullptr;
    dsc_ = lv_img_dsc_t{};
    Psram::release(buf_);
    buf_ = nullptr;
    d_ = 0;
    mode_ = Mode::NONE;
}

void PetDisc::showDepletion(uint8_t percentLeft) {
    if (percentLeft > 100) percentLeft = 100;
    if (mode_ == Mode::DEPLETION && percent_ == percentLeft) return;
    mode_ = Mode::DEPLETION;
    percent_ = percentLeft;
    paint();
}

void PetDisc::showBands(const uint32_t* colors, int count) {
    if (colors == nullptr || count < 1) return;
    if (count > 3) count = 3;

    if (mode_ == Mode::BANDS && bandCount_ == count) {
        bool same = true;
        for (int i = 0; i < count; ++i) same = same && (bands_[i] == colors[i]);
        if (same) return;
    }
    mode_ = Mode::BANDS;
    bandCount_ = count;
    for (int i = 0; i < count; ++i) bands_[i] = colors[i];
    paint();
}

void PetDisc::paint() {
    if (buf_ == nullptr || img_ == nullptr || d_ <= 0) return;

    const float r  = d_ * 0.5f;
    const float r2 = r * r;
    const float ringW  = (d_ / 12.0f > 1.0f) ? (d_ / 12.0f) : 1.0f;
    const float inner  = r - ringW;
    const float inner2 = inner * inner;
    const float darkDeg = (100 - percent_) * 3.6f;   // phần đã mất, tính từ 12 giờ theo chiều kim đồng hồ

    for (int y = 0; y < d_; ++y) {
        for (int x = 0; x < d_; ++x) {
            uint8_t* p = buf_ + (static_cast<size_t>(y) * d_ + x) * kPetBytesPerPx;
            const float dx = x + 0.5f - r;
            const float dy = y + 0.5f - r;
            const float dist2 = dx * dx + dy * dy;

            if (dist2 > r2) {
                petPutTransparent(p);
                continue;
            }

            // Góc theo chiều kim đồng hồ tính từ 12 giờ: 0 = trên, 90 = phải, 180 = dưới.
            float angle = std::atan2(dx, -dy) * kRadToDeg;
            if (angle < 0.0f) angle += 360.0f;

            if (mode_ == Mode::DEPLETION) {
                if (angle < darkDeg) {
                    petPutPixel(p, 0x000000, kMaskAlpha);
                } else {
                    petPutTransparent(p);
                }
            } else if (dist2 > inner2) {
                petPutPixel(p, kRingRgb, 0xFF);
            } else {
                int band = 0;
                if (bandCount_ >= 2) {
                    band = static_cast<int>(angle * bandCount_ / 360.0f);
                    if (band >= bandCount_) band = bandCount_ - 1;
                }
                petPutPixel(p, bands_[band], 0xFF);
            }
        }
    }
    lv_obj_invalidate(img_);
}
