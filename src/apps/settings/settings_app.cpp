// Settings: chon theme (hang dau) va chon toi da kMaxSelected app hien tren Main Screen.
//   2/8 = len/xuong (quay vong)   5 = ENTER: doi theme / bat tat app   4/6 = theme truoc/sau
//   1 = ESC ve Main Screen
// Moi thay doi duoc ghi xuong flash ngay (SettingsStore::save). Doi theme thi dung lai man hinh
// (AppManager::reload) de ap mau moi; vi tri hang dang chon duoc nho qua lan dung lai.

#include <lvgl.h>

#include <cstdio>

#include "config/display.hpp"
#include "core/app_base.hpp"
#include "core/app_factories.hpp"
#include "core/app_manager.hpp"
#include "core/app_registry.hpp"
#include "core/theme.hpp"
#include "services/input_service.hpp"
#include "services/settings_store.hpp"

namespace {

constexpr size_t kMaxRows = 1 + 16;  // hang theme + toi da 16 app chon duoc
constexpr int kRowH = UI_COMPACT ? 20 : 30;
constexpr int kTitleH = UI_COMPACT ? 16 : 24;
constexpr int kFooterH = UI_COMPACT ? 14 : 20;

int lastRow = 0;  // nho hang dang chon khi man hinh bi dung lai (doi theme)

class SettingsApp : public IApp {
 public:
  AppId id() const override { return AppId::APP_SETTINGS; }
  void onCreate(lv_obj_t* parent) override;
  void onDestroy() override {
    for (size_t i = 0; i < kMaxRows; ++i) {
      rows_[i] = nullptr;
      labels_[i] = nullptr;
    }
    footer_ = nullptr;
    nRows_ = 0;
  }

 private:
  static void onRowEvent(lv_event_t* e);
  int indexOf(const lv_obj_t* obj) const;
  void addRow(lv_obj_t* list);
  void refreshRow(size_t row);
  void refreshFooter(bool refused);
  void changeTheme(int dir);
  void toggleApp(size_t row);

  lv_obj_t* rows_[kMaxRows] = {nullptr};
  lv_obj_t* labels_[kMaxRows] = {nullptr};
  AppId rowApp_[kMaxRows] = {AppId::COUNT};
  size_t nRows_ = 0;
  lv_obj_t* footer_ = nullptr;
};

void SettingsApp::onCreate(lv_obj_t* parent) {
  const Theme& th = Themes::current();

  lv_obj_t* title = lv_label_create(parent);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_label_set_text(title, "Settings");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 1);

  lv_obj_t* list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, UI_W - 8, UI_H - kTitleH - kFooterH - 6);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, kTitleH + 2);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 2, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

  // Hang 0 = theme, sau do moi app chon duoc theo thu tu trong bang dang ky.
  rowApp_[nRows_] = AppId::COUNT;
  addRow(list);
  for (size_t i = 0; i < AppRegistry::count() && nRows_ < kMaxRows; ++i) {
    const AppInfo& a = AppRegistry::at(i);
    if (!a.selectable) continue;
    rowApp_[nRows_] = a.id;
    addRow(list);
  }

  footer_ = lv_label_create(parent);
  lv_obj_align(footer_, LV_ALIGN_BOTTOM_MID, 0, -2);
  (void)th;

  for (size_t r = 0; r < nRows_; ++r) refreshRow(r);
  refreshFooter(false);

  size_t focusRow = static_cast<size_t>(lastRow < 0 ? 0 : lastRow);
  if (focusRow >= nRows_) focusRow = 0;
  lv_group_focus_obj(rows_[focusRow]);
}

void SettingsApp::addRow(lv_obj_t* list) {
  const Theme& th = Themes::current();
  lv_obj_t* btn = lv_btn_create(list);  // nut tu vao group focus mac dinh
  lv_obj_set_size(btn, LV_PCT(100), kRowH);
  lv_obj_set_style_pad_all(btn, 2, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(th.panel), 0);
  lv_obj_set_style_text_color(btn, lv_color_hex(th.textOnPanel), 0);
  lv_obj_set_style_outline_color(btn, lv_color_hex(th.focus), LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUS_KEY);
  lv_obj_add_event_cb(btn, onRowEvent, LV_EVENT_ALL, this);

  lv_obj_t* lab = lv_label_create(btn);
  lv_obj_align(lab, LV_ALIGN_LEFT_MID, 2, 0);

  rows_[nRows_] = btn;
  labels_[nRows_] = lab;
  ++nRows_;
}

int SettingsApp::indexOf(const lv_obj_t* obj) const {
  if (!obj) return -1;
  for (size_t i = 0; i < nRows_; ++i) {
    if (rows_[i] == obj) return static_cast<int>(i);
  }
  return -1;
}

void SettingsApp::refreshRow(size_t row) {
  if (row >= nRows_ || !labels_[row]) return;
  char buf[40];
  if (row == 0) {
    snprintf(buf, sizeof(buf), "Theme: < %s >", Themes::current().name);
  } else {
    snprintf(buf, sizeof(buf), "[%c] %s", AppRegistry::isSelected(rowApp_[row]) ? 'x' : ' ',
             AppRegistry::info(rowApp_[row]).label);
  }
  lv_label_set_text(labels_[row], buf);
}

void SettingsApp::refreshFooter(bool refused) {
  if (!footer_) return;
  char buf[40];
  if (refused) {
    snprintf(buf, sizeof(buf), "Max %u apps", (unsigned)AppRegistry::kMaxSelected);
  } else {
    snprintf(buf, sizeof(buf), "Main Screen: %u/%u apps", (unsigned)AppRegistry::selectedCount(),
             (unsigned)AppRegistry::kMaxSelected);
  }
  lv_label_set_text(footer_, buf);
}

void SettingsApp::changeTheme(int dir) {
  const size_t n = Themes::count();
  size_t cur = 0;
  for (size_t i = 0; i < n; ++i) {
    if (&Themes::at(i) == &Themes::current()) cur = i;
  }
  const size_t next = (cur + n + (dir < 0 ? n - 1 : 1)) % n;
  Themes::select(Themes::at(next).key);
  SettingsStore::save();
  AppManager::reload();  // tri hoan: dung lai man hinh nay voi mau moi
}

void SettingsApp::toggleApp(size_t row) {
  const AppId id = rowApp_[row];
  const bool want = !AppRegistry::isSelected(id);
  if (!AppRegistry::setSelected(id, want)) {
    refreshFooter(true);  // da du so app toi da
    return;
  }
  SettingsStore::save();
  refreshRow(row);
  refreshFooter(false);
}

void SettingsApp::onRowEvent(lv_event_t* e) {
  auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
  const int idx = self->indexOf(lv_event_get_target(e));
  if (idx < 0) return;  // co the chay sau onDestroy()

  switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
      lastRow = idx;
      break;
    case LV_EVENT_CLICKED:  // phim 5
      if (idx == 0) {
        self->changeTheme(+1);
      } else {
        self->toggleApp(static_cast<size_t>(idx));
      }
      break;
    case LV_EVENT_KEY:
      switch (lv_event_get_key(e)) {
        case LV_KEY_UP:
          lv_group_focus_prev(InputService::defaultGroup());
          break;
        case LV_KEY_DOWN:
          lv_group_focus_next(InputService::defaultGroup());
          break;
        case LV_KEY_LEFT:
          if (idx == 0) self->changeTheme(-1);
          break;
        case LV_KEY_RIGHT:
          if (idx == 0) self->changeTheme(+1);
          break;
        case LV_KEY_ESC:
          AppManager::goHome();
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

}  // namespace

IApp* createSettingsApp() { return new SettingsApp(); }
