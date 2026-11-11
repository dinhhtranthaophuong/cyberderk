#pragma once
#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/app_base.hpp"

// Music Player: trai = dia quay + thanh tien do + nut dieu khien, phai = ten bai + playlist (/music/*.mp3).
// Phim: 2/8 chon bai, 4/6 tua -/+10 s, 5 phat bai dang chon hoac Play/Pause neu la bai dang phat,
//       7/9 bai truoc/sau, 3 (KEY_OPTION) tang am luong (10..100, buoc 10, quay vong), 1 ve Home.
//       Thoat app thi bai dang phat chay het roi dung (khong tu chuyen bai); tu chuyen bai chi khi app mo.
class MusicApp : public IApp {
 public:
  AppId id() const override { return AppId::APP_MUSIC_PLAYER; }
  void onCreate(lv_obj_t* parent) override;
  void onResume() override;
  void onPause() override;
  void onDestroy() override;
  bool onKey(uint32_t key) override;
  void onTick(uint32_t nowMs) override;

 private:
  static void keyCb(lv_event_t* e);
  static void sinkFocusCb(lv_event_t* e);

  void handleKey(uint32_t key);
  void moveCursor(int delta);
  void activateCursor();
  void playIndex(int idx);
  void stepTrack(int delta);
  void cycleVolume();

  void buildLeft(lv_obj_t* root);
  void buildRight(lv_obj_t* root);
  void rebuildPlaylist();
  void styleRow(size_t i);
  void refreshStatus(bool force);
  void applyCover();

  bool alive_ = false;
  bool armed_ = false;
  bool needRefocus_ = false;

  lv_obj_t* root_ = nullptr;
  lv_obj_t* nextSink_ = nullptr;
  lv_obj_t* prevSink_ = nullptr;
  lv_obj_t* discArcA_ = nullptr;
  lv_obj_t* discArcB_ = nullptr;
  lv_obj_t* bar_ = nullptr;
  lv_obj_t* timeLbl_ = nullptr;
  lv_obj_t* ctrlLbl_ = nullptr;
  lv_obj_t* titleLbl_ = nullptr;
  lv_obj_t* infoLbl_ = nullptr;
  lv_obj_t* list_ = nullptr;
  lv_obj_t* volLbl_ = nullptr;
  lv_obj_t* volBar_ = nullptr;
  lv_obj_t* discImg_ = nullptr;
  int discD_ = 0;            // canh khung anh mat dia (= duong kinh dia tru vien)
  int frameIdx_ = 0;
  bool hasCover_ = false;
  bool coverChecked_ = true;
  std::vector<lv_obj_t*> rows_;

  std::vector<std::string> paths_;
  std::vector<std::string> names_;
  int cursor_ = 0;
  int playingIdx_ = -1;

  int spinDeg_ = 0;
  uint32_t lastSpinMs_ = 0;
  uint32_t lastUiMs_ = 0;
  uint32_t lastShownSec_ = 0xFFFFFFFFu;
  int lastState_ = -1;
  int lastVolShown_ = -1;
  int lastCursorShown_ = -1;
};