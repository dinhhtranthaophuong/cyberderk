#pragma once
#include <cstdint>

struct DateTime {
    uint16_t year; uint8_t month, day;   // month 1-12
    uint8_t hour, minute, second;
    uint8_t weekday;                     // 0 = Chủ Nhật
};

namespace TimeService {
// Khởi tạo I2C (Pins::I2cSda/I2cScl, 100 kHz) và dò DS3231 ở địa chỉ 0x68.
// Trả true khi có DS3231 VÀ giờ trong chip đáng tin (cờ OSF không bật, giá trị hợp lệ).
// Trả false khi không có chip, chip lỗi, hoặc chip vừa mất nguồn pin (OSF = 1): khi đó
// now() dùng giờ hệ thống cho tới khi có set() hoặc syncNtp() thành công.
// Gọi trong setup(), không cần LvglLock. Gọi lại an toàn (trả kết quả lần đầu).
// Đặt múi giờ hệ thống mặc định "ICT-7" và nạp giờ RTC vào đồng hồ hệ thống.
bool begin();

// Giờ ĐỊA PHƯƠNG. Đọc DS3231 (cache tối đa 200 ms để nhiều nơi gọi không dồn I2C);
// lỗi I2C, chưa begin(), hoặc giờ chưa đáng tin thì trả giờ hệ thống. Không chặn lâu, gọi được từ mọi task.
DateTime now();

// Ghi giờ ĐỊA PHƯƠNG vào DS3231, xóa cờ OSF và đặt luôn giờ hệ thống.
// Giá trị không hợp lệ (năm ngoài 2000-2099, ngày không tồn tại...) bị bỏ qua.
// `weekday` do service tự tính từ ngày, giá trị người gọi truyền vào bị bỏ qua.
void set(const DateTime& dt);

// Đồng bộ NTP rồi ghi vào DS3231. YÊU CẦU WiFi đã kết nối sẵn (service không quản lý WiFi).
// BLOCK tới ~8 giây: chỉ gọi từ task nền (Core 0), KHÔNG gọi từ task LVGL / loop().
// tz: chuỗi POSIX TZ, ví dụ "ICT-7"; nullptr hoặc rỗng = "ICT-7". Trả true nếu đồng bộ thành công.
bool syncNtp(const char* tz);
}