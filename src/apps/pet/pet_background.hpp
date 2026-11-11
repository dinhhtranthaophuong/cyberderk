#pragma once
// PetBackground: nền động của app Pet: bầu trời có mây trôi, đồng cỏ xanh với các vệt vàng.
// Ban ngày hoặc ban đêm (trăng, sao nhấp nháy, mây xám, cỏ tối) khi mèo đang ngủ.
//
// Cấu tạo (tất cả nằm trong PSRAM, vẽ MỘT lần lúc create):
//   - canvas ngày và canvas đêm phủ kín màn hình (trời, trăng, cỏ, vệt cỏ). Canvas đêm nằm trên
//     và đổi độ trong suốt nên chuyển ngày/đêm mờ dần (~1 s), không phải vẽ lại gì.
//   - mỗi đám mây là một canvas nhỏ (nền trong suốt), tick() chỉ đổi vị trí; ban đêm đổi màu
//     bằng recolor. Mỗi ngôi sao cũng là một canvas nhỏ, tick() đổi độ sáng để nhấp nháy.
// Nhờ vậy mỗi nhịp chỉ phải vẽ lại vùng nhỏ quanh mây/sao. Mọi kích thước tính theo UI_W/UI_H.
//
// Chỉ gọi từ ngữ cảnh UI (đang giữ LvglLock), không gọi từ Core 0.

#include <lvgl.h>
#include <cstdint>
#include "config/display.hpp"

class PetBackground {
public:
    static constexpr int kClouds = 4;
    static constexpr int kStars  = 9;

    PetBackground() = default;
    ~PetBackground();   // chỉ giải phóng buffer, không đụng LVGL (app phải gọi destroy() trong onDestroy)
    PetBackground(const PetBackground&) = delete;
    PetBackground& operator=(const PetBackground&) = delete;

    // Tạo các lớp nền dưới `parent`. Nên gọi TRƯỚC khi tạo các object khác để nền nằm dưới cùng.
    // Trả false nếu hết PSRAM cho nền chính (mây/sao thiếu thì bỏ qua, các hàm khác vẫn an toàn).
    bool create(lv_obj_t* parent, bool night = false);

    // Xóa object, giải phóng buffer, đặt con trỏ về nullptr. Gọi nhiều lần cũng được.
    void destroy();

    // Đổi ngày/đêm: bắt đầu mờ dần sang chế độ mới (tick() thực hiện chuyển cảnh).
    void setNight(bool night);
    bool night() const { return night_; }

    // Mây trôi, sao nhấp nháy, chuyển cảnh. Gọi thường xuyên (onTick); tự giới hạn nhịp ~20 Hz.
    void tick(uint32_t nowMs);

    lv_obj_t* obj() const { return day_; }

private:
    struct Cloud {
        lv_obj_t* obj  = nullptr;
        uint8_t*  buf  = nullptr;
        int       w    = 0;
        int       y    = 0;
        int32_t   x256 = 0;   // vị trí ngang, đơn vị 1/256 px
    };
    struct Star {
        lv_obj_t* obj = nullptr;
        uint8_t*  buf = nullptr;
    };

    void updateStars(uint32_t nowMs);

    lv_obj_t* day_     = nullptr;
    uint8_t*  dayBuf_  = nullptr;   // PSRAM, sở hữu bởi PetBackground
    lv_obj_t* nightC_  = nullptr;
    uint8_t*  nightBuf_ = nullptr;
    Cloud     clouds_[kClouds];
    Star      stars_[kStars];

    bool      night_    = false;    // chế độ đang hướng tới
    int       target_   = 0;        // 0 = ngày, 255 = đêm
    int       fade_     = 0;        // mức chuyển cảnh hiện tại 0..255
    bool      started_  = false;
    uint32_t  lastMs_   = 0;
};
