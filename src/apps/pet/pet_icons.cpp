#include "apps/pet/pet_icons.hpp"

#include <Arduino.h>
#include "apps/pet/pet_art.hpp"
#include "apps/pet/pet_pixel.hpp"
#include "core/psram.hpp"

static_assert(PetIcons::kGrid == kPetIconGrid, "Luoi icon trong PetIcons phai khop pet_art.hpp");

PetIcons::~PetIcons() {
    Psram::release(buf_);
    buf_ = nullptr;
}

bool PetIcons::create() {
    destroy();

    const size_t one = static_cast<size_t>(kSize) * kSize * kPetBytesPerPx;
    buf_ = static_cast<uint8_t*>(Psram::alloc(one * kCount));
    if (buf_ == nullptr) {
        Serial.println("[PetIcons] khong du PSRAM cho icon");
        return false;
    }

    for (int i = 0; i < kCount; ++i) {
        uint8_t* data = buf_ + one * i;
        const PetIcon icon = static_cast<PetIcon>(i);
        petRenderGrid(data, kGrid, kScale, [icon](int gx, int gy, uint32_t* rgb) {
            return petIconColor(icon, gx, gy, rgb);
        });
        petInitDsc(&dsc_[i], data, kSize, kSize);
    }
    return true;
}

void PetIcons::destroy() {
    for (int i = 0; i < kCount; ++i) dsc_[i] = lv_img_dsc_t{};
    Psram::release(buf_);
    buf_ = nullptr;
}

const lv_img_dsc_t* PetIcons::get(PetIcon icon) const {
    const int i = static_cast<int>(icon);
    if (buf_ == nullptr || i < 0 || i >= kCount) return nullptr;
    return &dsc_[i];
}
