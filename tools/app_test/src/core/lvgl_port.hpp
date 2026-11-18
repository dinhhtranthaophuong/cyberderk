#pragma once
#include <lvgl.h>

namespace LvglPort {
// Khoi tao TFT + LVGL (display driver, buffer ve trong PSRAM) va tao task LVGL chay tren Core 1.
// Phai goi truoc InputService::init(). Sau khi goi, task LVGL chay ngay, nen moi thao tac
// LVGL trong setup() (InputService::init, AppManager::init) phai nam trong LvglLock.
bool init();

// Chay lv_timer_handler() duoi LvglLock. Task LVGL goi ham nay; khong can goi tay
// (test tren may tinh goi truc tiep).
void tick();
}  // namespace LvglPort

// Khoa LVGL (RAII, de quy). Chi can khi code ngoai task LVGL buoc phai cham vao LVGL;
// quy tac chung van la KHONG goi LVGL tu Core 0 (xem ARCHITECTURE.md muc 5).
class LvglLock {
 public:
  LvglLock();
  ~LvglLock();
  LvglLock(const LvglLock&) = delete;
  LvglLock& operator=(const LvglLock&) = delete;
};
