#pragma once
// PetState: chỉ số và trạng thái của thú ảo. Logic thuần (không LVGL, không Arduino)
// nên test được trên máy tính. Thời gian truyền vào dưới dạng millis().
//
// Dữ liệu nằm ở MỘT đối tượng toàn cục (instance()), KHÔNG nằm trong PetApp, vì PetApp
// bị hủy mỗi lần thoát. Nhờ vậy thú vẫn "đói dần" khi người dùng ở màn hình khác, và
// khi vào lại app, update() tính bù phần thời gian đã trôi qua. Thú bắt đầu tính giờ
// từ lần đầu mở app. Lưu qua lúc tắt máy: nối SdStorage ở bước sau (hiện chỉ lưu RAM).
//
// Quy tắc chính:
//   - HUNGER, FUN, CLEAN giảm dần theo thời gian (nửa tốc độ khi mèo đang ngủ).
//   - ENERGY giảm khi thức; hết một vòng (về 0) thì mèo tự đi ngủ (trời đêm). Khi ngủ
//     ENERGY hồi dần, đầy thì tự dậy (trời sáng).
//   - Một trong ba chỉ số no/vui/sạch có mặt nạ tối quá 3/5 (giá trị dưới 40) thì mèo buồn.

#include <cstdint>
#include "apps/pet/pet_types.hpp"   // PetMood, PetSkin, PetStat (giá trị 0..100, 100 = tốt)

class PetState {
public:
    static constexpr uint8_t kMax = 100;

    static PetState& instance();

    // Gọi thường xuyên (onCreate, onTick). Lần gọi đầu chỉ ghi mốc thời gian.
    void update(uint32_t nowMs);

    uint8_t value(PetStat stat) const;

    // Hành động. nowMs dùng để đặt mốc tâm trạng "vui" trong ~2 giây.
    // Trả false (không làm gì) nếu mèo đang ngủ.
    bool eat(uint32_t nowMs);    // no +30, sạch -5
    bool play(uint32_t nowMs);   // vui +25, no -8
    bool bath(uint32_t nowMs);   // sạch +40

    // Nút Sleep: đang ngủ thì đánh thức (năng lượng giữ nguyên), đang thức thì đi ngủ.
    // Trả false (không làm gì) nếu đang thức mà năng lượng đã đầy (chưa buồn ngủ).
    bool toggleSleep();
    bool sleeping() const { return asleep_; }

    PetSkin skin() const { return skin_; }
    void nextSkin();             // xoay vòng: tam thể, tuxedo, trắng, cam vằn

    // Tâm trạng hiện tại: SLEEPING khi đang ngủ, HAPPY ngay sau hành động, SAD khi một trong
    // ba chỉ số no/vui/sạch dưới 40, còn lại NORMAL.
    PetMood mood() const;

private:
    static constexpr int kStatCount = static_cast<int>(PetStat::COUNT);

    PetState();
    void add(PetStat stat, int delta);
    void makeHappy(uint32_t nowMs);
    void updateEnergy(uint32_t dtMs);

    uint8_t  val_[kStatCount];
    uint32_t acc_[kStatCount] = {};   // phần dư (ms) chưa đủ một điểm
    uint32_t lastMs_ = 0;
    bool     started_ = false;
    uint32_t happyUntil_ = 0;
    bool     happy_ = false;
    bool     asleep_ = false;
    PetSkin  skin_ = PetSkin::CALICO;
};
