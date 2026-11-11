#include "apps/main_screen/slideshow_widget.hpp"

#include "core/theme.hpp"

void SlideshowWidget::create(lv_obj_t* parent) {
  const Theme& th = Themes::current();

  frame_ = lv_obj_create(parent);
  lv_obj_remove_style_all(frame_);
  lv_obj_clear_flag(frame_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(frame_, kW, kH);
  lv_obj_align(frame_, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_color(frame_, lv_color_hex(th.panel), 0);  // khung trong theo theme
  lv_obj_set_style_bg_opa(frame_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(frame_, 6, 0);
  lv_obj_set_style_clip_corner(frame_, true, 0);

  // Vien khi duoc focus: ve de len tren anh (border_post), phang, khong glow.
  lv_obj_set_style_border_post(frame_, true, 0);
  lv_obj_set_style_border_width(frame_, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(frame_, lv_color_hex(th.focus), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(frame_, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);

  // Chu co dinh khi chua co anh.
  hint_ = lv_label_create(frame_);
  lv_label_set_text(hint_, "dinhh py");
  lv_obj_set_style_text_color(hint_, lv_color_hex(th.textOnPanel), 0);
  lv_obj_center(hint_);

  img_ = lv_img_create(frame_);
  lv_obj_add_flag(img_, LV_OBJ_FLAG_HIDDEN);

  // Tao sau cung de nam tren anh. Khoi scrim lam chu doc duoc tren moi anh.
  clock_ = lv_label_create(frame_);
  lv_obj_set_width(clock_, lv_pct(100));
  lv_obj_set_style_text_align(clock_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(clock_, lv_color_hex(th.text), 0);
  lv_obj_set_style_bg_color(clock_, lv_color_hex(th.scrim), 0);
  lv_obj_set_style_bg_opa(clock_, th.scrimOpa, 0);
  lv_obj_set_style_pad_ver(clock_, 1, 0);
  lv_label_set_text(clock_, "");
  lv_obj_align(clock_, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void SlideshowWidget::destroy() {
  frame_ = nullptr;
  hint_ = nullptr;
  img_ = nullptr;
  clock_ = nullptr;
}

void SlideshowWidget::setImage(const lv_img_dsc_t* dsc) {
  if (!img_ || !hint_) return;
  if (dsc) {
    lv_img_set_src(img_, dsc);
    lv_obj_center(img_);
    lv_obj_clear_flag(img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
  } else {
    // Chi an anh (khong goi set_src(nullptr)); nguoi goi co the free pixels sau khi ham nay tra ve.
    lv_obj_add_flag(img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hint_, LV_OBJ_FLAG_HIDDEN);
  }
}

void SlideshowWidget::setClockText(const char* text) {
  if (clock_) lv_label_set_text(clock_, text);
}