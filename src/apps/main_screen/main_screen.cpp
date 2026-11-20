// Main Screen (launcher): nua trai = SlideshowWidget (anh + dong ho, focus duoc, phim 5 mo Album),
// nua phai = luoi icon 2 cot x 3 hang. Vong focus theo phim 4/6: widget trai -> cot 1 -> cot 2 ->
// widget trai. Phim 2/8 chi doi hang trong luoi (quay vong). Nho vi tri focus khi quay lai.
// Gio: TimeService::now() (DS3231, gio dia phuong; khong co chip thi service tu dung gio he thong).

#include "apps/main_screen/main_screen.hpp"

#include <lvgl.h>
#include <cstring>
#include "core/theme.hpp"


#include <cstdio>

#include "apps/main_screen/slideshow_widget.hpp"
#include "config/display.hpp"
#include "core/app_manager.hpp"
#include "core/app_registry.hpp"
#include "services/input_service.hpp"
#include "services/slideshow_service.hpp"
#include "services/time_service.hpp"

static_assert(GRID_COLS == 2 && GRID_ROWS == 3, "Main Screen dung luoi 2 cot x 3 hang");

namespace {

constexpr size_t kSlots = AppRegistry::kLauncherSlots;

// Chọn bộ icon theo khóa theme: night = dark blue (0), mint (1), pink (2), theme Giáng sinh (3).
// Theme chưa có bộ icon riêng thì dùng bộ 0.
uint8_t iconSet() {
  const char* k = Themes::current().key;
  if (std::strcmp(k, "mint") == 0) return 1;
  if (std::strcmp(k, "pink") == 0) return 2;
  if (std::strcmp(k, "christmas gift") == 0 || std::strcmp(k, "christmas") == 0 || std::strcmp(k, "xmas") == 0) return 3;  // đổi thành khóa thật trong theme.cpp
  return 0;  // "night" và các theme khác
}

constexpr int kLeftFocus = -1;  // gia tri lastFocusIdx khi widget trai dang focus
constexpr uint32_t kClockCheckMs = 1000;

const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Nho vi tri focus de khi ve Main Screen thi giu nguyen.
int lastFocusIdx = 0;  // 0..kSlots-1 = o luoi, kLeftFocus = widget trai
int lastGridIdx = 0;   // o luoi focus gan nhat (de biet vao luoi o hang nao tu widget trai)

class MainScreenApp : public IApp {
 public:
  AppId id() const override { return AppId::MAIN_SCREEN; }
  void onCreate(lv_obj_t* parent) override;
  void onTick(uint32_t nowMs) override;
  void onDestroy() override {
    left_.destroy();
    for (size_t i = 0; i < kSlots; ++i) items_[i] = nullptr;
  }

 private:
  static void onItemEvent(lv_event_t* e);
  static void onLeftEvent(lv_event_t* e);
  int indexOf(const lv_obj_t* obj) const;
  void moveFocus(int idx, uint32_t key);
  void focusItem(int idx);
  void focusLeft();
  void updateClock();
  void pollImage();

  SlideshowWidget left_;
  lv_obj_t* items_[kSlots] = {nullptr};
  uint32_t lastClockMs_ = 0;
  bool clockStarted_ = false;
  int32_t lastMinuteKey_ = -1;
  bool imageSettled_ = false;
};

void MainScreenApp::onCreate(lv_obj_t* parent) {
  // Nua trai. Container lv_obj khong tu vao group nen them thu cong (truoc cac nut -> NEXT di tu trai sang phai).
  left_.create(parent);
  lv_group_add_obj(InputService::defaultGroup(), left_.frame());
  lv_obj_add_event_cb(left_.frame(), onLeftEvent, LV_EVENT_ALL, this);
  updateClock();

  SlideshowService::request(SlideshowWidget::kW, SlideshowWidget::kH);
  pollImage();

  // Nua phai: luoi icon 2 cot x 3 hang.
  lv_obj_t* grid = lv_obj_create(parent);
  lv_obj_remove_style_all(grid);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grid, UI_W / 2 - 6, UI_H - 8);
  lv_obj_align(grid, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_pad_row(grid, 3, 0);
  lv_obj_set_style_pad_column(grid, 3, 0);

  static lv_coord_t colDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t rowDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, colDsc, rowDsc);

  const AppId* slots = AppRegistry::launcherSlots();
  const Theme& th = Themes::current();
  for (size_t i = 0; i < kSlots; ++i) {
    lv_obj_t* btn = lv_btn_create(grid);
    items_[i] = btn;
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, static_cast<lv_coord_t>(i % GRID_COLS), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<lv_coord_t>(i / GRID_COLS), 1);
    lv_obj_set_style_pad_all(btn, 2, 0);
    lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(btn, onItemEvent, LV_EVENT_ALL, this);
    lv_obj_set_style_bg_color(btn, lv_color_hex(th.panel), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);  // phẳng, không glow
    lv_obj_set_style_outline_color(btn, lv_color_hex(th.focus), LV_STATE_FOCUS_KEY);

        // Màn 320x240: icon 64x64 ở giữa nút. App chưa có icon hoặc màn nhỏ: hiện tên như cũ.
        const void* icon = UI_COMPACT ? nullptr : AppRegistry::icon(slots[i], iconSet());
    if (icon) {
      lv_obj_t* img = lv_img_create(btn);
      lv_img_set_src(img, icon);
      lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_center(img);
    } else {
      const AppInfo& info = AppRegistry::info(slots[i]);
      if (info.label[0] != '\0') {  // o trong thi khong co nhan
        lv_obj_t* lab = lv_label_create(btn);
        lv_label_set_text(lab, info.label);
        lv_obj_set_style_text_color(lab, lv_color_hex(th.textOnPanel), 0);
        lv_obj_center(lab);
      }
    }
  }

  if (lastFocusIdx == kLeftFocus) {
    focusLeft();
  } else {
    focusItem(lastFocusIdx);
  }
}

void MainScreenApp::pollImage() {
  if (imageSettled_ || !left_.frame()) return;
  if (!SlideshowService::settled()) return;
  imageSettled_ = true;
  left_.setImage(SlideshowService::image());  // nullptr -> hiện chữ "dinhh py"
}

void MainScreenApp::onTick(uint32_t nowMs) {
  pollImage();
  if (clockStarted_ && (nowMs - lastClockMs_) < kClockCheckMs) return;
  clockStarted_ = true;
  lastClockMs_ = nowMs;
  updateClock();
}

// Chi sua label khi phut doi.
void MainScreenApp::updateClock() {
  if (!left_.frame()) return;
  const DateTime now = TimeService::now();
  // Khoa phut tang nghiem ngat theo thoi gian (nam toi da 2099 van vua int32_t).
  const int32_t key = (((static_cast<int32_t>(now.year) * 12 + (now.month - 1)) * 31 + (now.day - 1)) * 1440) +
                      now.hour * 60 + now.minute;
  if (key == lastMinuteKey_) return;
  lastMinuteKey_ = key;

  const char* mon = kMonths[(now.month + 11) % 12];  // month 1..12 -> 0..11, an toan ca khi month = 0
  char buf[24];
  if (UI_COMPACT) {  // man 160x128: nua trai chi ~74 px, tach 2 dong
    snprintf(buf, sizeof(buf), "%s %d\n%02d:%02d", mon, now.day, now.hour, now.minute);
  } else {
    snprintf(buf, sizeof(buf), "%s %d, %02d:%02d", mon, now.day, now.hour, now.minute);
  }
  left_.setClockText(buf);
}

int MainScreenApp::indexOf(const lv_obj_t* obj) const {
  if (!obj) return -1;
  for (size_t i = 0; i < kSlots; ++i) {
    if (items_[i] == obj) return static_cast<int>(i);
  }
  return -1;
}

void MainScreenApp::focusItem(int idx) {
  if (idx < 0 || idx >= static_cast<int>(kSlots) || !items_[idx]) return;
  lv_group_focus_obj(items_[idx]);
}

void MainScreenApp::focusLeft() {
  if (left_.frame()) lv_group_focus_obj(left_.frame());
}

// Di chuyen focus trong luoi. Phim trai/phai o cot bien -> nhay sang widget trai.
// Phim len/xuong quay vong (LVGL 8.3 khong tu lam viec nay cho phim mui ten).
void MainScreenApp::moveFocus(int idx, uint32_t key) {
  int row = idx / GRID_COLS;
  int col = idx % GRID_COLS;
  switch (key) {
    case LV_KEY_LEFT:
      if (col == 0) {
        focusLeft();
        return;
      }
      --col;
      break;
    case LV_KEY_RIGHT:
      if (col == GRID_COLS - 1) {
        focusLeft();
        return;
      }
      ++col;
      break;
    case LV_KEY_UP:
      row = (row + GRID_ROWS - 1) % GRID_ROWS;
      break;
    case LV_KEY_DOWN:
      row = (row + 1) % GRID_ROWS;
      break;
    default:
      return;  // ESC va phim khac: Main la man goc nen khong lam gi
  }
  focusItem(row * GRID_COLS + col);
}

void MainScreenApp::onItemEvent(lv_event_t* e) {
  auto* self = static_cast<MainScreenApp*>(lv_event_get_user_data(e));
  const int idx = self->indexOf(lv_event_get_target(e));
  if (idx < 0) return;  // co the chay sau onDestroy()

  switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
      lastFocusIdx = idx;
      lastGridIdx = idx;
      break;
    case LV_EVENT_CLICKED: {
      const AppId target = AppRegistry::launcherSlots()[idx];
      if (target != AppId::COUNT) AppManager::goTo(target);  // o trong: khong lam gi
      break;
    }
    case LV_EVENT_KEY:
      self->moveFocus(idx, lv_event_get_key(e));
      break;
    default:
      break;
  }
}

void MainScreenApp::onLeftEvent(lv_event_t* e) {
  auto* self = static_cast<MainScreenApp*>(lv_event_get_user_data(e));
  lv_obj_t* frame = self->left_.frame();
  if (!frame || lv_event_get_target(e) != frame) return;  // co the chay sau onDestroy()

  switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
      lastFocusIdx = kLeftFocus;
      break;
    case LV_EVENT_CLICKED:
      AppManager::goTo(AppId::APP_ALBUM);
      break;
    case LV_EVENT_KEY: {
      const int row = lastGridIdx / GRID_COLS;
      const uint32_t key = lv_event_get_key(e);
      if (key == LV_KEY_RIGHT) {
        self->focusItem(row * GRID_COLS);  // vao cot dau cua luoi
      } else if (key == LV_KEY_LEFT) {
        self->focusItem(row * GRID_COLS + GRID_COLS - 1);  // quay vong: cot cuoi
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace

IApp* createMainScreenApp() { return new MainScreenApp(); }