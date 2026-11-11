#pragma once
// PetDisc: một đĩa tròn nhỏ (một lv_img) đặt giữa nút, dùng cho hai việc:
//   1. showDepletion(): mặt nạ tối hình quạt phủ lên icon, thể hiện chỉ số còn lại.
//      100% = không phủ, 50% = phủ nửa vòng bên phải, 0% = phủ kín. Phần tối lan dần theo
//      chiều kim đồng hồ, bắt đầu từ 12 giờ (giống hiệu ứng cooldown trong game).
//   2. showBands(): ô màu lông cho nút Skin, 1 màu hoặc 2 đến 3 dải quạt (tam thể), có viền sáng.
//
// Điểm ảnh tự vẽ vào buffer PSRAM (không dùng lv_arc) nên kết quả không phụ thuộc vào cách
// LVGL xử lý góc cung. Chỉ vẽ lại khi giá trị đổi. Chỉ gọi từ ngữ cảnh UI.

#include <lvgl.h>
#include <cstdint>

class PetDisc {
public:
    PetDisc() = default;
    ~PetDisc();   // chỉ giải phóng buffer; không đụng LVGL (app phải gọi destroy() trong onDestroy)
    PetDisc(const PetDisc&) = delete;
    PetDisc& operator=(const PetDisc&) = delete;

    // Tạo lv_img đường kính `diameter` px, căn giữa trong `parent`. Trả false nếu hết PSRAM.
    bool create(lv_obj_t* parent, int diameter);

    // Xóa object, giải phóng buffer, đặt con trỏ về nullptr. Gọi nhiều lần cũng được.
    void destroy();

    void showDepletion(uint8_t percentLeft);              // 0..100
    void showBands(const uint32_t* colors, int count);    // count 1..3, màu 0xRRGGBB

    lv_obj_t* obj() const { return img_; }

private:
    enum class Mode : uint8_t { NONE, DEPLETION, BANDS };
    void paint();

    lv_obj_t*    img_ = nullptr;
    uint8_t*     buf_ = nullptr;   // PSRAM, sở hữu bởi PetDisc
    lv_img_dsc_t dsc_{};
    int          d_ = 0;

    Mode     mode_ = Mode::NONE;
    uint8_t  percent_ = 100;
    int      bandCount_ = 0;
    uint32_t bands_[3] = {0, 0, 0};
};
