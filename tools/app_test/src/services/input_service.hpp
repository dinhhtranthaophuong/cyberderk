#pragma once
#include <lvgl.h>

namespace InputService {
// Tao lv_indev (keypad) va group mac dinh. Goi SAU LvglPort::init() va TRUOC AppManager::init(),
// de widget tao ra sau do tu vao group mac dinh. Goi khi dang giu LvglLock.
//
// Nguon phim: ban phim ma tran 3x3 that (services/keypad_matrix) va Serial Monitor
// (go '1'..'9', 'h'), ca hai doc trong read_cb cua lv_indev, cung di qua route().
//  - Phim thuong va ESC (phim 1) di vao lv_indev nhu phim LVGL.
//  - KEY_OPTION (phim 3) va KEY_HOME ('h' / giu phim 1) di qua AppManager::postKey().
void init();

lv_indev_t* indev();
lv_group_t* defaultGroup();  // app them widget can focus vao group nay
}  // namespace InputService
