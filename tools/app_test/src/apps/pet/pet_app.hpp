#pragma once
// App Pet (APP_PET): thú ảo. Mèo đứng trên cánh đồng, 6 nút tròn hai bên:
//   trái:  Eat, Play, Bath  (mỗi nút có mặt nạ tối thể hiện chỉ số tương ứng)
//   phải:  Sleep (vòng năng lượng: hết một vòng thì trời tối, mèo ngủ), Skin (đổi màu lông), nút trống
// Chỉ số và màu lông nằm ở PetState (toàn cục), không nằm trong PetApp.

#include <lvgl.h>
#include <cstdint>
#include "core/app_base.hpp"
#include "apps/pet/pet_background.hpp"
#include "apps/pet/pet_disc.hpp"
#include "apps/pet/pet_icons.hpp"
#include "apps/pet/pet_sprite.hpp"
#include "apps/pet/pet_state.hpp"

class PetApp : public IApp {
public:
    PetApp() = default;
    ~PetApp() override = default;

    AppId id() const override { return AppId::APP_PET; }

    void onCreate(lv_obj_t* parent) override;
    void onResume() override;
    void onPause() override;
    void onDestroy() override;
    bool onKey(uint32_t key) override;     // KEY_OPTION (menu phụ, chưa dùng)
    void onTick(uint32_t nowMs) override;

private:
    // 6 nút: 3 bên trái (index 0..2, trên xuống), 3 bên phải (index 3..5).
    // index = cột * 3 + hàng, cột 0 = trái, cột 1 = phải.
    static constexpr int kSlots = 6;
    static constexpr int kRows  = 3;
    static constexpr int kCols  = 2;
    static constexpr int kMasks = 4;   // Eat, Play, Bath, Sleep có mặt nạ chỉ số

    enum Slot : int { SLOT_EAT = 0, SLOT_PLAY, SLOT_BATH, SLOT_SLEEP, SLOT_SKIN, SLOT_SPARE };

    static void onButtonEvent(lv_event_t* e);   // callback chung cho cả 6 nút
    int  indexOf(lv_obj_t* obj) const;          // -1 nếu không phải nút của app (hoặc đã destroy)
    void handleKey(int idx, uint32_t key);
    void moveFocus(int from, int dRow, int dCol);
    void activate(int idx);
    void refresh();      // cập nhật mặt nạ chỉ số và biểu cảm mèo từ PetState
    void applySkin();    // màu lông của mèo và ô màu trên nút Skin theo PetState

    lv_obj_t* root_ = nullptr;
    lv_obj_t* btns_[kSlots] = {};
    PetBackground background_;
    PetSprite sprite_;
    PetIcons icons_;
    PetDisc masks_[kMasks];
    PetDisc swatch_;
    bool destroying_ = false;
    uint32_t lastLogMs_ = 0;
};
