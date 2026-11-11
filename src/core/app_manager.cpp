#include "core/app_manager.hpp"

#include <Arduino.h>

#include <atomic>

#include "config/display.hpp"
#include "config/keymap.hpp"
#include "core/app_factories.hpp"
#include "core/app_registry.hpp"
#include "core/lvgl_port.hpp"
#include "core/psram.hpp"
#include "core/theme.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "services/input_service.hpp"

namespace {

constexpr uint8_t kNone = static_cast<uint8_t>(AppId::COUNT);

IApp* app = nullptr;
lv_obj_t* screen = nullptr;
AppId currentId = AppId::COUNT;  // COUNT = chua co app nao
std::atomic<uint8_t> pending{kNone};
std::atomic<bool> reloadPending{false};
QueueHandle_t keyQueue = nullptr;

// Man hinh thay the cho app chua cai dat. Co mot doi tuong nhan phim de ESC ve Main Screen.
class PlaceholderApp : public IApp {
 public:
  explicit PlaceholderApp(AppId id) : id_(id) {}
  AppId id() const override { return id_; }

  void onCreate(lv_obj_t* parent) override {
    lv_obj_t* holder = lv_obj_create(parent);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, UI_W, UI_H);
    lv_group_add_obj(InputService::defaultGroup(), holder);
    lv_obj_add_event_cb(holder, onKeyEvent, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(holder);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_label_set_text(title, AppRegistry::info(id_).label);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t* sub = lv_label_create(holder);
    lv_label_set_text(sub, "(chua cai dat)");
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 2);

    lv_obj_t* hint = lv_label_create(holder);
    lv_label_set_text(hint, "1: back   h: home");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 22);
  }

  void onDestroy() override {}

 private:
  static void onKeyEvent(lv_event_t* e) {
    if (lv_event_get_key(e) == LV_KEY_ESC) AppManager::goHome();
  }

  AppId id_;
};

IApp* createApp(AppId id) {
  IApp* a = nullptr;
  switch (id) {
    case AppId::MAIN_SCREEN:
      if (createMainScreenApp) a = createMainScreenApp();
      break;
    case AppId::APP_PET:
      if (createPetApp) a = createPetApp();
      break;
    case AppId::APP_MUSIC_PLAYER:
      if (createMusicApp) a = createMusicApp();
      break;
    case AppId::APP_VISUALIZER:
      if (createVisualizerApp) a = createVisualizerApp();
      break;
    case AppId::APP_CALENDAR:
      if (createCalendarApp) a = createCalendarApp();
      break;
    case AppId::APP_ALBUM:
      if (createAlbumApp) a = createAlbumApp();
      break;
    case AppId::APP_SETTINGS:
      if (createSettingsApp) a = createSettingsApp();
      break;
    default:
      break;
  }
  return a ? a : new PlaceholderApp(id);
}

// Chuyen app. Luon goi khi dang giu LvglLock.
void switchTo(AppId id) {
  const uint32_t t0 = millis();

  // 1) Dung app cu (neu co). CHUA delete: widget cua no van con callback tro toi `this`, va
  //    LVGL con gui su kien (DEFOCUSED, DELETE) trong luc don group / huy man hinh.
  //    Vi vay app cu chi bi delete o buoc 4, sau khi man hinh cu da bi huy.
  lv_obj_t* oldScreen = screen ? screen : lv_scr_act();
  IApp* oldApp = app;
  if (oldApp) {
    oldApp->onPause();
    oldApp->onDestroy();
  }

  // 2) Man hinh moi: nen den, tat scroll; group focus sach de nut dau tien cua app moi nhan focus.
  lv_obj_t* scr = lv_obj_create(nullptr);
  // Nen, mau chu mac dinh (chu sang tren nen toi, tranh loi "man hinh den") va tat scroll
  // deu lay tu theme dang chon.
  Themes::applyToScreen(scr);
  lv_group_remove_all_objs(InputService::defaultGroup());

  // 3) App moi.
  IApp* next = createApp(id);
  next->onCreate(scr);
  lv_scr_load(scr);
  next->onResume();

  // 4) Huy man hinh cu roi moi delete app cu.
  lv_obj_del(oldScreen);
  delete oldApp;

  app = next;
  screen = scr;
  currentId = id;

  Serial.printf("[AppManager] -> %s (%u ms) | heap %u KB, psram %u KB\n",
                AppRegistry::info(id).label, (unsigned)(millis() - t0),
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(Psram::freeBytes() / 1024));
}

void processKeys() {
  if (!keyQueue) return;
  uint32_t key = 0;
  while (xQueueReceive(keyQueue, &key, 0) == pdTRUE) {
    if (key == KEY_HOME) {
      AppManager::goHome();
    } else if (app) {
      app->onKey(key);
    }
  }
}

}  // namespace

namespace AppManager {

void init() {
  keyQueue = xQueueCreate(8, sizeof(uint32_t));
  LvglLock lock;
  switchTo(AppId::MAIN_SCREEN);
}

void goTo(AppId id) {
  const uint8_t v = static_cast<uint8_t>(id);
  if (v >= kNone) return;
  pending.store(v);
}

void goHome() { goTo(AppId::MAIN_SCREEN); }

void reload() { reloadPending.store(true); }

AppId current() { return currentId; }

void tick(uint32_t nowMs) {
  LvglLock lock;
  processKeys();
  const bool reload = reloadPending.exchange(false);
  const uint8_t p = pending.exchange(kNone);
  if (p != kNone && static_cast<AppId>(p) != currentId) {
    switchTo(static_cast<AppId>(p));  // man hinh moi tu dung theme hien tai
  } else if (reload && currentId != AppId::COUNT) {
    switchTo(currentId);
  }
  if (app) app->onTick(nowMs);
}

void postKey(uint32_t key) {
  if (keyQueue) xQueueSend(keyQueue, &key, 0);
}

}  // namespace AppManager
