#pragma once
#include <lvgl.h>

#include <cstdint>

enum class AppId : uint8_t {
  MAIN_SCREEN,
  APP_PET,
  APP_MUSIC_PLAYER,
  APP_VISUALIZER,
  APP_CALENDAR,
  APP_ALBUM,
  APP_SETTINGS,
  COUNT
};

// Giao dien chung cua moi app (ARCHITECTURE.md muc 6.2).
// Cac ham onCreate/onResume/onPause/onDestroy/onKey/onTick luon duoc goi khi DANG GIU LvglLock,
// nen duoc phep goi LVGL truc tiep ben trong.
class IApp {
 public:
  virtual ~IApp() = default;
  virtual AppId id() const = 0;

  // Tao toan bo object LVGL duoi `parent` + cap phat tai nguyen.
  virtual void onCreate(lv_obj_t* parent) = 0;
  // Duoc hien thi / nhan focus (bat dau timer, task nen).
  virtual void onResume() {}
  // Mat focus (dung timer; KHONG giai phong tru khi onDestroy).
  virtual void onPause() {}
  // Don sach object LVGL + bo nho + task cua app. Khong duoc ro ri.
  virtual void onDestroy() = 0;
  // Phim custom (KEY_OPTION...). Tra true neu da xu ly.
  virtual bool onKey(uint32_t /*key*/) { return false; }
  // Goi dinh ky tren Core 1 (non-blocking), tuy chon.
  virtual void onTick(uint32_t /*nowMs*/) {}
};
