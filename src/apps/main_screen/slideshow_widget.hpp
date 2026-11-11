#pragma once
// Khung nua trai Main Screen: anh slideshow + dong ho de len tren.
// Chi tao/sua object LVGL khi dang giu LvglLock (onCreate/onTick/event callback deu da giu san).
#include <lvgl.h>

#include "config/display.hpp"

class SlideshowWidget {
 public:
  // Kich thuoc khung, dung de giai ma anh cho vua (Chat 4 truyen vao ImageService::decode).
  static constexpr int kW = UI_W / 2 - 6;
  static constexpr int kH = UI_H - 8;

  void create(lv_obj_t* parent);
  // Dat con tro ve nullptr. Object LVGL do screen cha huy, khong xoa o day.
  void destroy();

  lv_obj_t* frame() const { return frame_; }

  // Hien anh; nullptr = hien lai placeholder. KHONG so huu `dsc`: nguoi goi giu dsc + pixels
  // song cho den khi goi setImage lan sau hoac app bi huy (goi setImage(nullptr) truoc khi free).
  void setImage(const lv_img_dsc_t* dsc);
  void setClockText(const char* text);

 private:
  lv_obj_t* frame_ = nullptr;
  lv_obj_t* hint_ = nullptr;
  lv_obj_t* img_ = nullptr;
  lv_obj_t* clock_ = nullptr;
};