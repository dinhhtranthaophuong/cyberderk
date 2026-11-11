#pragma once
// STUB LVGL 8.3 chỉ đủ cho test VisualizerApp trên máy tính. KHÔNG phải LVGL thật.
// Mô phỏng: cây object, flag HIDDEN, kích thước/vị trí, event callback, group + focus
// (đúng ngữ nghĩa LVGL 8.3: NEXT/PREV do indev dời focus, phím khác gửi LV_EVENT_KEY tới object đang focus).

#include <cstddef>
#include <cstdint>
#include <vector>

typedef int16_t lv_coord_t;
typedef uint8_t lv_opa_t;
typedef uint32_t lv_style_selector_t;
typedef uint32_t lv_obj_flag_t;

struct lv_color_t {
  uint8_t r, g, b;
};
struct lv_point_t {
  lv_coord_t x, y;
};
struct lv_img_dsc_t {
  int dummy;
};

enum { LV_OPA_TRANSP = 0, LV_OPA_COVER = 255 };
enum {
  LV_OBJ_FLAG_HIDDEN = (1 << 0),
  LV_OBJ_FLAG_CLICKABLE = (1 << 1),
  LV_OBJ_FLAG_SCROLLABLE = (1 << 4),
};
enum {
  LV_KEY_UP = 17,
  LV_KEY_DOWN = 18,
  LV_KEY_RIGHT = 19,
  LV_KEY_LEFT = 20,
  LV_KEY_ESC = 27,
  LV_KEY_ENTER = 10,
  LV_KEY_NEXT = 9,
  LV_KEY_PREV = 11,
};
enum lv_event_code_t { LV_EVENT_ALL = 0, LV_EVENT_KEY = 1, LV_EVENT_FOCUSED = 2, LV_EVENT_DEFOCUSED = 3 };

struct lv_obj_t;
struct lv_group_t;
struct lv_event_t {
  lv_obj_t* target;
  lv_event_code_t code;
  void* user_data;
  uint32_t key;
};
typedef void (*lv_event_cb_t)(lv_event_t*);

struct lv_event_dsc_t {
  lv_event_cb_t cb;
  lv_event_code_t filter;
  void* user_data;
};

struct lv_obj_t {
  lv_obj_t* parent = nullptr;
  std::vector<lv_obj_t*> kids;
  int x = 0, y = 0, w = 0, h = 0;
  uint32_t flags = 0;
  lv_opa_t bg_opa = 0;
  lv_color_t bg_color = {0, 0, 0};
  bool is_line = false;
  const lv_point_t* pts = nullptr;
  int pts_n = 0;
  int line_w = 0;
  std::vector<lv_event_dsc_t> cbs;
  lv_group_t* group = nullptr;
};

struct lv_group_t {
  std::vector<lv_obj_t*> objs;
  int focus = -1;
};

// ---- stub: kiểm soát vòng đời để test đếm rò rỉ ----
extern int g_stub_live_objs;

lv_obj_t* lv_obj_create(lv_obj_t* parent);
lv_obj_t* lv_line_create(lv_obj_t* parent);
void lv_obj_del(lv_obj_t* obj);   // hủy cả cây, tự gỡ khỏi group

void lv_obj_remove_style_all(lv_obj_t* o);
void lv_obj_set_pos(lv_obj_t* o, lv_coord_t x, lv_coord_t y);
void lv_obj_set_size(lv_obj_t* o, lv_coord_t w, lv_coord_t h);
void lv_obj_set_x(lv_obj_t* o, lv_coord_t x);
void lv_obj_set_y(lv_obj_t* o, lv_coord_t y);
void lv_obj_set_width(lv_obj_t* o, lv_coord_t w);
void lv_obj_set_height(lv_obj_t* o, lv_coord_t h);
void lv_obj_add_flag(lv_obj_t* o, lv_obj_flag_t f);
void lv_obj_clear_flag(lv_obj_t* o, lv_obj_flag_t f);
bool lv_obj_has_flag(const lv_obj_t* o, lv_obj_flag_t f);

void lv_obj_set_style_bg_opa(lv_obj_t* o, lv_opa_t v, lv_style_selector_t sel);
void lv_obj_set_style_bg_color(lv_obj_t* o, lv_color_t c, lv_style_selector_t sel);
void lv_obj_set_style_line_width(lv_obj_t* o, lv_coord_t w, lv_style_selector_t sel);
void lv_obj_set_style_line_color(lv_obj_t* o, lv_color_t c, lv_style_selector_t sel);
void lv_obj_set_style_line_rounded(lv_obj_t* o, bool r, lv_style_selector_t sel);
void lv_line_set_points(lv_obj_t* o, const lv_point_t pts[], uint16_t n);

lv_color_t lv_color_hex(uint32_t c);
lv_color_t lv_color_black();
lv_color_t lv_color_white();

lv_event_dsc_t* lv_obj_add_event_cb(lv_obj_t* o, lv_event_cb_t cb, lv_event_code_t filter, void* user_data);
void* lv_event_get_user_data(lv_event_t* e);
lv_event_code_t lv_event_get_code(lv_event_t* e);
lv_obj_t* lv_event_get_target(lv_event_t* e);
uint32_t lv_event_get_key(lv_event_t* e);
void lv_stub_send_event(lv_obj_t* o, lv_event_code_t code, uint32_t key);   // dùng trong test

void lv_group_add_obj(lv_group_t* g, lv_obj_t* o);
void lv_group_focus_obj(lv_obj_t* o);
void lv_group_focus_next(lv_group_t* g);
void lv_group_focus_prev(lv_group_t* g);
lv_obj_t* lv_group_get_focused(const lv_group_t* g);
void lv_group_remove_all_objs(lv_group_t* g);
