#include "apps/pet/pet_sprite.hpp"

#include <Arduino.h>
#include "apps/pet/pet_art.hpp"
#include "apps/pet/pet_pixel.hpp"
#include "core/psram.hpp"

static_assert(PetSprite::kGrid == kPetGrid, "Luoi meo trong PetSprite phai khop pet_art.hpp");

namespace {
constexpr uint32_t kShadowDayRgb   = 0x007A2A;   // xanh lá đậm: bóng trên cỏ ban ngày
constexpr uint32_t kShadowNightRgb = 0x0B2E15;   // xanh rất đậm: bóng trên cỏ ban đêm
}

PetSprite::~PetSprite() {
    // Không đụng LVGL ở đây: object (nếu còn) đã bị hủy cùng screen của app.
    Psram::release(buf_);
    buf_ = nullptr;
}

bool PetSprite::create(lv_obj_t* parent, PetMood mood, PetSkin skin) {
    destroy();
    if (parent == nullptr) return false;

    mood_ = (mood < PetMood::COUNT) ? mood : PetMood::NORMAL;
    skin_ = (skin < PetSkin::COUNT) ? skin : PetSkin::CALICO;

    const size_t bytes = static_cast<size_t>(kSize) * kSize * kPetBytesPerPx;
    buf_ = static_cast<uint8_t*>(Psram::alloc(bytes));
    if (buf_ == nullptr) {
        Serial.println("[PetSprite] khong du PSRAM cho anh meo");
        return false;
    }

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, kSize, kSize);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_CLICKABLE);

    // Bóng dưới chân, tạo trước để nằm dưới ảnh mèo.
    shadow_ = lv_obj_create(root_);
    lv_obj_remove_style_all(shadow_);
    lv_obj_set_size(shadow_, 14 * kScale, 2 * kScale);
    lv_obj_set_pos(shadow_, 3 * kScale, 18 * kScale);
    lv_obj_set_style_radius(shadow_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_, lv_color_hex(night_ ? kShadowNightRgb : kShadowDayRgb), 0);
    lv_obj_set_style_bg_opa(shadow_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(shadow_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(shadow_, LV_OBJ_FLAG_CLICKABLE);

    petInitDsc(&dsc_, buf_, kSize, kSize);
    render();

    img_ = lv_img_create(root_);
    lv_obj_set_pos(img_, 0, 0);
    lv_img_set_src(img_, &dsc_);
    return true;
}

void PetSprite::destroy() {
    // Xóa object trước, rồi mới giải phóng buffer mà ảnh đang trỏ tới.
    if (root_ != nullptr) {
        lv_obj_del(root_);   // xóa luôn bóng và ảnh (con của root_)
    }
    root_ = nullptr;
    shadow_ = nullptr;
    img_ = nullptr;
    dsc_ = lv_img_dsc_t{};
    Psram::release(buf_);
    buf_ = nullptr;
}

void PetSprite::setMood(PetMood mood) {
    if (mood >= PetMood::COUNT || mood == mood_) return;
    mood_ = mood;
    refresh();
}

void PetSprite::setSkin(PetSkin skin) {
    if (skin >= PetSkin::COUNT || skin == skin_) return;
    skin_ = skin;
    refresh();
}

void PetSprite::setNight(bool night) {
    if (night == night_) return;
    night_ = night;
    if (shadow_ != nullptr) {
        lv_obj_set_style_bg_color(shadow_, lv_color_hex(night_ ? kShadowNightRgb : kShadowDayRgb), 0);
    }
}

namespace {
constexpr uint32_t kBlinkMs      = 150;    // thời gian khép mắt
constexpr uint32_t kBlinkGapMs   = 3000;   // chớp lại sau 3 s + ngẫu nhiên 0..2,5 s
constexpr uint32_t kBlinkJitter  = 2500;
constexpr uint32_t kEarMs        = 250;    // thời gian tai cụp
constexpr uint32_t kEarGapMs     = 3500;   // giật tai lại sau 4 s + ngẫu nhiên 0..3 s
constexpr uint32_t kEarJitter    = 3000;
constexpr uint32_t kTailCalmMs   = 700;    // chu kỳ nhấc đuôi khi bình thường
constexpr uint32_t kTailHappyMs  = 200;    // chu kỳ nhấc đuôi khi vui
constexpr uint32_t kBreathMs     = 1800;   // một nửa nhịp thở khi ngủ
constexpr uint32_t kFxMs         = 2000;   // thời gian phản ứng khi bấm nút
constexpr uint32_t kTearMs       = 400;    // nhịp rơi của giọt nước mắt
}  // namespace

uint32_t PetSprite::nextRand(uint32_t n) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (n == 0) ? 0 : (rng_ % n);
}

void PetSprite::react(PetFx fx, uint32_t nowMs) {
    if (mood_ == PetMood::SLEEPING) return;
    fx_ = fx;
    fxStart_ = nowMs;
    fxEnd_ = nowMs + kFxMs;
}

void PetSprite::tick(uint32_t nowMs) {
    if (buf_ == nullptr) return;
    if (!animInit_) {
        animInit_ = true;
        blinkAt_ = nowMs + kBlinkGapMs;
        earAt_   = nowMs + kEarGapMs;
    }

    PetAnim a;
    if (mood_ == PetMood::SLEEPING) {
        a.breathe = ((nowMs / kBreathMs) & 1u) != 0;
    } else {
        // So sánh kiểu ép int32 để đúng cả khi millis() tràn số.
        const int32_t sinceBlink = static_cast<int32_t>(nowMs - blinkAt_);
        if (sinceBlink >= 0) {
            if (sinceBlink < static_cast<int32_t>(kBlinkMs)) a.blink = true;
            else blinkAt_ = nowMs + kBlinkGapMs + nextRand(kBlinkJitter);
        }
        const int32_t sinceEar = static_cast<int32_t>(nowMs - earAt_);
        if (sinceEar >= 0) {
            if (sinceEar < static_cast<int32_t>(kEarMs)) a.earTwitch = true;
            else earAt_ = nowMs + kEarGapMs + nextRand(kEarJitter);
        }
        if (mood_ != PetMood::SAD) {   // buồn thì đuôi rủ, không vẫy
            const uint32_t period = (mood_ == PetMood::HAPPY) ? kTailHappyMs : kTailCalmMs;
            a.tailUp = ((nowMs / period) & 1u) != 0;
        }
    }

        // Phản ứng khi bấm nút và giọt nước mắt.
    if (mood_ == PetMood::SLEEPING || static_cast<int32_t>(nowMs - fxEnd_) >= 0) fx_ = PetFx::NONE;
    if (fx_ != PetFx::NONE) {
        const uint32_t period = (fx_ == PetFx::EAT) ? 150u : (fx_ == PetFx::PLAY) ? 200u : 250u;
        const uint32_t phase = (nowMs - fxStart_) / period;
        a.fx = fx_;
        a.fxPhase = static_cast<uint8_t>((fx_ == PetFx::BATH) ? phase % 6 : phase & 1u);
    }
    if (mood_ == PetMood::SAD) a.tear = static_cast<uint8_t>((nowMs / kTearMs) % 3);

    if (a != anim_) {
        anim_ = a;
        if (root_ != nullptr) {   // PLAY: nhảy lên, BATH: rung qua lại
            int tx = 0, ty = 0;
            if (a.fx == PetFx::PLAY && a.fxPhase) ty = -2 * kScale;
            if (a.fx == PetFx::BATH) tx = (a.fxPhase & 1u) ? kScale : -kScale;
            lv_obj_set_style_translate_x(root_, tx, 0);
            lv_obj_set_style_translate_y(root_, ty, 0);
        }
        refresh();
    }
}

void PetSprite::refresh() {
    if (buf_ == nullptr) return;
    render();
    if (img_ != nullptr) {
        lv_img_set_src(img_, &dsc_);   // đặt lại nguồn để LVGL vẽ lại
        lv_obj_invalidate(img_);
    }
}

void PetSprite::render() {
    if (buf_ == nullptr) return;
    petRenderGrid(buf_, kGrid, kScale, [this](int gx, int gy, uint32_t* rgb) {
        return petBodyColor(gx, gy, mood_, skin_, rgb, anim_);
    });
}
