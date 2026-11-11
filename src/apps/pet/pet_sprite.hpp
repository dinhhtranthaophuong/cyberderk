#pragma once
// PetSprite: con mèo pixel của app Pet.
//
// Hình mèo là lưới 20x20 nằm trong pet_art.cpp (thuần dữ liệu). Ở đây chỉ vẽ lưới đó
// vào một buffer RGB565+alpha trong PSRAM rồi hiển thị bằng MỘT lv_img. Không có object
// LVGL cho từng khối màu. Đổi biểu cảm hay màu lông = vẽ lại buffer (rẻ).
//
// Toàn bộ hàm ở đây chỉ gọi từ ngữ cảnh UI (đang giữ LvglLock), không gọi từ Core 0.

#include <lvgl.h>
#include <cstdint>
#include "config/display.hpp"
#include "apps/pet/pet_types.hpp"
#include "apps/pet/pet_art.hpp"   // PetAnim

class PetSprite {
public:
    static constexpr int kGrid  = 20;                      // lưới 20x20 ô (khớp kPetGrid)
    static constexpr int kScale = UI_COMPACT ? 3 : 6;      // 160x128: x3 (60 px), 320x240: x6 (120 px)
    static constexpr int kSize  = kGrid * kScale;          // cạnh ảnh mèo, px

    PetSprite() = default;
    ~PetSprite();   // chỉ giải phóng buffer PSRAM, không đụng LVGL (app phải gọi destroy() trong onDestroy)
    PetSprite(const PetSprite&) = delete;
    PetSprite& operator=(const PetSprite&) = delete;

    // Tạo object LVGL (container + bóng + ảnh) dưới `parent` và cấp buffer PSRAM.
    // Trả false nếu hết PSRAM (khi đó obj() == nullptr, các hàm khác vẫn an toàn).
    bool create(lv_obj_t* parent, PetMood mood = PetMood::NORMAL, PetSkin skin = PetSkin::CALICO);

    // Xóa object, giải phóng buffer, đặt con trỏ về nullptr. Gọi nhiều lần cũng được.
    void destroy();

    // Chỉ vẽ lại khi giá trị thực sự đổi.
    void setMood(PetMood mood);
    void setSkin(PetSkin skin);
    void setNight(bool night);   // đổi màu bóng dưới chân cho hợp nền đêm
    PetMood mood() const { return mood_; }
    PetSkin skin() const { return skin_; }

        // Hoạt ảnh: chớp mắt, giật tai, vẫy đuôi (khi thức), thở (khi ngủ). Chỉ vẽ lại khi khung đổi.
    void tick(uint32_t nowMs);
    
    // Phản ứng khi bấm Eat/Play/Bath (kéo dài ~2 s). Bỏ qua nếu mèo đang ngủ.
    void react(PetFx fx, uint32_t nowMs);
    // Container gốc của mèo (để app căn vị trí). nullptr nếu chưa create.
    lv_obj_t* obj() const { return root_; }

    static constexpr int width()  { return kSize; }
    static constexpr int height() { return kSize; }

private:
    void render();
    void refresh();   // vẽ lại buffer và báo LVGL vẽ lại ảnh
    uint32_t nextRand(uint32_t n);   // số giả ngẫu nhiên nhỏ trong [0, n)

    lv_obj_t*     root_   = nullptr;
    lv_obj_t*     shadow_ = nullptr;
    lv_obj_t*     img_    = nullptr;
    uint8_t*      buf_    = nullptr;   // PSRAM, sở hữu bởi PetSprite
    lv_img_dsc_t  dsc_{};
    PetMood       mood_   = PetMood::NORMAL;
    PetSkin       skin_   = PetSkin::CALICO;
    bool          night_  = false;
    
    // Hoạt ảnh
    PetAnim       anim_{};
    bool          animInit_ = false;
    PetFx         fx_       = PetFx::NONE;
    uint32_t      fxStart_  = 0;
    uint32_t      fxEnd_    = 0;
    uint32_t      blinkAt_  = 0;     // mốc bắt đầu lần chớp tiếp theo
    uint32_t      earAt_    = 0;     // mốc bắt đầu lần giật tai tiếp theo
    uint32_t      rng_      = 2463534242u;
};
