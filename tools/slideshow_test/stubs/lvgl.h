#pragma once
// Stub LVGL 8.3: dung dung bo cuc lv_img_dsc_t (header co bitfield w:11 h:11).
#include <cstdint>
#define LV_IMG_CF_TRUE_COLOR 4
typedef struct {
  uint32_t cf : 5;
  uint32_t always_zero : 3;
  uint32_t reserved : 2;
  uint32_t w : 11;
  uint32_t h : 11;
} lv_img_header_t;
typedef struct {
  lv_img_header_t header;
  uint32_t data_size;
  const uint8_t* data;
} lv_img_dsc_t;
