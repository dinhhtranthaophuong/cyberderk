#include "lvgl.h"

#include <algorithm>

int g_stub_live_objs = 0;

lv_obj_t* lv_obj_create(lv_obj_t* parent) {
  lv_obj_t* o = new lv_obj_t();
  o->parent = parent;
  if (parent) parent->kids.push_back(o);
  ++g_stub_live_objs;
  return o;
}

lv_obj_t* lv_line_create(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  o->is_line = true;
  return o;
}

static void group_remove(lv_obj_t* o);

void lv_obj_del(lv_obj_t* obj) {
  if (!obj) return;
  while (!obj->kids.empty()) lv_obj_del(obj->kids.back());
  group_remove(obj);
  if (obj->parent) {
    std::vector<lv_obj_t*>& k = obj->parent->kids;
    k.erase(std::remove(k.begin(), k.end(), obj), k.end());
  }
  --g_stub_live_objs;
  delete obj;
}

void lv_obj_remove_style_all(lv_obj_t* o) {
  o->bg_opa = 0;
  o->line_w = 0;
}
void lv_obj_set_pos(lv_obj_t* o, lv_coord_t x, lv_coord_t y) {
  o->x = x;
  o->y = y;
}
void lv_obj_set_size(lv_obj_t* o, lv_coord_t w, lv_coord_t h) {
  o->w = w;
  o->h = h;
}
void lv_obj_set_x(lv_obj_t* o, lv_coord_t x) { o->x = x; }
void lv_obj_set_y(lv_obj_t* o, lv_coord_t y) { o->y = y; }
void lv_obj_set_width(lv_obj_t* o, lv_coord_t w) { o->w = w; }
void lv_obj_set_height(lv_obj_t* o, lv_coord_t h) { o->h = h; }
void lv_obj_add_flag(lv_obj_t* o, lv_obj_flag_t f) { o->flags |= f; }
void lv_obj_clear_flag(lv_obj_t* o, lv_obj_flag_t f) { o->flags &= ~f; }
bool lv_obj_has_flag(const lv_obj_t* o, lv_obj_flag_t f) { return (o->flags & f) != 0; }

void lv_obj_set_style_bg_opa(lv_obj_t* o, lv_opa_t v, lv_style_selector_t) { o->bg_opa = v; }
void lv_obj_set_style_bg_color(lv_obj_t* o, lv_color_t c, lv_style_selector_t) { o->bg_color = c; }
void lv_obj_set_style_line_width(lv_obj_t* o, lv_coord_t w, lv_style_selector_t) { o->line_w = w; }
void lv_obj_set_style_line_color(lv_obj_t*, lv_color_t, lv_style_selector_t) {}
void lv_obj_set_style_line_rounded(lv_obj_t*, bool, lv_style_selector_t) {}
void lv_line_set_points(lv_obj_t* o, const lv_point_t pts[], uint16_t n) {
  o->pts = pts;
  o->pts_n = n;
}

lv_color_t lv_color_hex(uint32_t c) {
  lv_color_t r = {static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c)};
  return r;
}
lv_color_t lv_color_black() {
  lv_color_t r = {0, 0, 0};
  return r;
}
lv_color_t lv_color_white() {
  lv_color_t r = {255, 255, 255};
  return r;
}

lv_event_dsc_t* lv_obj_add_event_cb(lv_obj_t* o, lv_event_cb_t cb, lv_event_code_t filter, void* user_data) {
  lv_event_dsc_t d = {cb, filter, user_data};
  o->cbs.push_back(d);
  return &o->cbs.back();
}
void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }
lv_event_code_t lv_event_get_code(lv_event_t* e) { return e->code; }
lv_obj_t* lv_event_get_target(lv_event_t* e) { return e->target; }
uint32_t lv_event_get_key(lv_event_t* e) { return e->key; }

void lv_stub_send_event(lv_obj_t* o, lv_event_code_t code, uint32_t key) {
  // Sao chép danh sách callback: callback có thể làm đổi focus/đăng ký thêm.
  std::vector<lv_event_dsc_t> copy = o->cbs;
  for (size_t i = 0; i < copy.size(); ++i) {
    if (copy[i].filter == LV_EVENT_ALL || copy[i].filter == code) {
      lv_event_t e = {o, code, copy[i].user_data, key};
      copy[i].cb(&e);
    }
  }
}

// ---- group: ngữ nghĩa của LVGL 8.3 ----
void lv_group_focus_obj(lv_obj_t* o) {
  lv_group_t* g = o ? o->group : nullptr;
  if (!g) return;
  int idx = -1;
  for (size_t i = 0; i < g->objs.size(); ++i)
    if (g->objs[i] == o) idx = static_cast<int>(i);
  if (idx < 0) return;
  if (g->focus == idx) return;
  lv_obj_t* old = g->focus >= 0 ? g->objs[g->focus] : nullptr;
  if (old) lv_stub_send_event(old, LV_EVENT_DEFOCUSED, 0);
  g->focus = idx;   // cập nhật trước khi gửi FOCUSED, giống LVGL
  lv_stub_send_event(o, LV_EVENT_FOCUSED, 0);
}

void lv_group_add_obj(lv_group_t* g, lv_obj_t* o) {
  o->group = g;
  g->objs.push_back(o);
  if (g->focus < 0) {
    g->focus = -1;
    lv_group_focus_obj(o);
  }
}

void lv_group_focus_next(lv_group_t* g) {
  if (g->objs.empty()) return;
  int n = static_cast<int>(g->objs.size());
  lv_group_focus_obj(g->objs[(g->focus + 1) % n]);
}
void lv_group_focus_prev(lv_group_t* g) {
  if (g->objs.empty()) return;
  int n = static_cast<int>(g->objs.size());
  lv_group_focus_obj(g->objs[(g->focus - 1 + n) % n]);
}
lv_obj_t* lv_group_get_focused(const lv_group_t* g) {
  return (g->focus >= 0 && g->focus < static_cast<int>(g->objs.size())) ? g->objs[g->focus] : nullptr;
}

void lv_group_remove_all_objs(lv_group_t* g) {
  if (g->focus >= 0) lv_stub_send_event(g->objs[g->focus], LV_EVENT_DEFOCUSED, 0);
  for (size_t i = 0; i < g->objs.size(); ++i) g->objs[i]->group = nullptr;
  g->objs.clear();
  g->focus = -1;
}

static void group_remove(lv_obj_t* o) {
  lv_group_t* g = o->group;
  if (!g) return;
  int idx = -1;
  for (size_t i = 0; i < g->objs.size(); ++i)
    if (g->objs[i] == o) idx = static_cast<int>(i);
  if (idx < 0) return;
  const bool wasFocused = (g->focus == idx);
  g->objs.erase(g->objs.begin() + idx);
  o->group = nullptr;
  if (g->objs.empty()) {
    g->focus = -1;
  } else if (wasFocused) {
    g->focus = -1;
    lv_group_focus_obj(g->objs[idx % g->objs.size()]);   // LVGL: xóa object đang focus thì dời focus
  } else if (g->focus > idx) {
    --g->focus;
  }
}
