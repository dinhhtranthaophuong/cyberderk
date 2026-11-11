#include "apps/music/music_app.hpp"

#include <algorithm>
#include <cstdio>

#include "apps/music/disc_cover.hpp"
#include "config/display.hpp"
#include "config/keymap.hpp"
#include "core/app_manager.hpp"
#include "core/theme.hpp"
#include "services/audio_service.hpp"
#include "services/input_service.hpp"
#include "services/sd_storage.hpp"

namespace {

constexpr size_t kMaxTracks = 64;
// Hai cung quay (khi chua co anh): ~20 fps, ~80 do/giay.
constexpr uint32_t kSpinPeriodMs = 50;
constexpr int kSpinStepDeg = 4;
// Anh mat dia: doi sang khung dung san ke tiep moi 100 ms (DiscCover::kStepDeg = 8 do => ~80 do/giay).
constexpr uint32_t kCoverFramePeriodMs = 100;
constexpr int kDiscBorder = 2;
constexpr uint32_t kUiPeriodMs = 200;
constexpr int kSeekSeconds = 10;
// Am luong hien thi (UI) 10..100, buoc 10. UI 100 = muc 80 cua AudioService (muc cao hon bi re tren loa nay).
constexpr int kVolumeUiStep = 10;
constexpr int kVolumeUiMax = 100;
constexpr int kVolumeMaxInternal = 80;

// Trang thai giu qua cac lan vao/ra app (nhac chay nen nen app moi can biet bai nao dang phat).
std::string sPlayingPath;
int sVolume = 50;   // muc UI

void applyVolume() { AudioService::setVolume((uint8_t)(sVolume * kVolumeMaxInternal / kVolumeUiMax)); }

lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// Bo khoi group focus (arc, bar, label co the tu vao group mac dinh).
void noFocus(lv_obj_t* o) { lv_group_remove_obj(o); }

// Doi tuong phang: khong vien, khong nen, khong outline focus, khong cuon.
void flatten(lv_obj_t* o) {
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_outline_width(o, 0, 0);
  lv_obj_set_style_outline_width(o, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(o, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of('/');
  std::string n = (slash == std::string::npos) ? path : path.substr(slash + 1);
  size_t dot = n.find_last_of('.');
  if (dot != std::string::npos && dot > 0) n.resize(dot);
  return n;
}

void fmtTime(char* out, size_t cap, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(out, cap, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

}  // namespace

// ---------------------------------------------------------------- vong doi
void MusicApp::onCreate(lv_obj_t* parent) {
  alive_ = true;
  armed_ = false;
  needRefocus_ = false;

  // Idempotent: lan dau mo Music se khoi dong I2S + task. Co the chuyen sang main.cpp sau.
  AudioService::begin();
  applyVolume();
  // Roi Music luc bai dang phat thi bai chay het roi tat (khong tu chuyen bai). Co "bai vua ket thuc"
  // co the con ton tu luc app vang mat: xoa di de khong bi nhay sang bai ke tiep khi vao lai.
  AudioService::finished();

  const Theme& th = Themes::current();

  // Playlist
  std::vector<std::string> all = SdStorage::listFiles("/music", ".mp3");
  for (const std::string& p : all) {
    if (paths_.size() >= kMaxTracks) break;
    paths_.push_back(p);
    names_.push_back(baseName(p));
  }
  cursor_ = 0;
  playingIdx_ = -1;
  AudioState st = AudioService::state();
  if (!sPlayingPath.empty()) {
    for (size_t i = 0; i < paths_.size(); i++) {
      if (paths_[i] == sPlayingPath) {
        cursor_ = (int)i;
        if (st == AudioState::PLAYING || st == AudioState::PAUSED) playingIdx_ = (int)i;
        break;
      }
    }
  }

  // Root nhan phim. Thu tu group: root, nextSink, prevSink.
  // LVGL tieu thu LV_KEY_NEXT/PREV (doi focus), nen 9 = focus_next -> nextSink, 7 = focus_prev -> prevSink.
  root_ = lv_obj_create(parent);
  flatten(root_);
  lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(root_, 0, 0);
  lv_obj_set_style_text_color(root_, col(th.text), 0);

  lv_group_t* g = InputService::defaultGroup();
  lv_group_add_obj(g, root_);

  nextSink_ = lv_obj_create(root_);
  flatten(nextSink_);
  lv_obj_set_size(nextSink_, 1, 1);
  lv_obj_set_pos(nextSink_, 0, 0);
  lv_group_add_obj(g, nextSink_);

  prevSink_ = lv_obj_create(root_);
  flatten(prevSink_);
  lv_obj_set_size(prevSink_, 1, 1);
  lv_obj_set_pos(prevSink_, 0, 0);
  lv_group_add_obj(g, prevSink_);

  buildLeft(root_);
  buildRight(root_);

  lv_obj_add_event_cb(root_, keyCb, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(nextSink_, keyCb, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(prevSink_, keyCb, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(nextSink_, sinkFocusCb, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(prevSink_, sinkFocusCb, LV_EVENT_FOCUSED, this);

  lv_group_focus_obj(root_);
  armed_ = true;

  rebuildPlaylist();
  refreshStatus(true);

  // Anh mat dia: giai ma nen o Core 0, onTick se gan anh khi xong.
  coverChecked_ = false;
  hasCover_ = false;
  DiscCover::request(discD_, Themes::current().bg);
  if (DiscCover::settled()) applyCover();
}

void MusicApp::applyCover() {
  coverChecked_ = true;
  const lv_img_dsc_t* dsc = DiscCover::frame(0);
  if (!dsc || !discImg_) return;   // khong co anh: giu hai cung quay
  frameIdx_ = 0;
  lv_img_set_src(discImg_, dsc);
  lv_obj_center(discImg_);
  lv_obj_clear_flag(discImg_, LV_OBJ_FLAG_HIDDEN);
  if (discArcA_) lv_obj_add_flag(discArcA_, LV_OBJ_FLAG_HIDDEN);
  if (discArcB_) lv_obj_add_flag(discArcB_, LV_OBJ_FLAG_HIDDEN);
  hasCover_ = true;
}

void MusicApp::onResume() {
  lastUiMs_ = 0;
  lastSpinMs_ = 0;
  lastShownSec_ = 0xFFFFFFFFu;
  lastState_ = -1;
}

void MusicApp::onPause() {}

void MusicApp::onDestroy() {
  // Nhac KHONG dung o day (chay nen). Chi don phan cua app.
  alive_ = false;
  armed_ = false;
  root_ = nextSink_ = prevSink_ = nullptr;
  discArcA_ = discArcB_ = discImg_ = nullptr;
  hasCover_ = false;
  coverChecked_ = true;
  bar_ = timeLbl_ = ctrlLbl_ = titleLbl_ = infoLbl_ = list_ = nullptr;
  volLbl_ = volBar_ = nullptr;
  rows_.clear();
  paths_.clear();
  names_.clear();
}

bool MusicApp::onKey(uint32_t key) {
  if (key == KEY_OPTION) {
    cycleVolume();
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- giao dien
void MusicApp::buildLeft(lv_obj_t* root) {
  const Theme& th = Themes::current();
  const int leftW = UI_W / 2;
  const int pad = UI_COMPACT ? 4 : 8;
  const int discD = std::min(leftW - 2 * pad, UI_H / 2);
  const int discX = (leftW - discD) / 2;

  // Than dia
  lv_obj_t* disc = lv_obj_create(root);
  flatten(disc);
  noFocus(disc);
  lv_obj_set_size(disc, discD, discD);
  lv_obj_set_pos(disc, discX, pad);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, col(th.panel), 0);
  lv_obj_set_style_border_width(disc, kDiscBorder, 0);
  lv_obj_set_style_border_color(disc, col(th.accent), 0);

  // Hai cung quay cho thay dia dang quay (xoay bang lv_arc_set_rotation trong onTick)
  const int arcD = discD - (UI_COMPACT ? 8 : 16);
  const int arcW = std::max(3, discD / 14);
  lv_obj_t* arcs[2];
  for (int i = 0; i < 2; i++) {
    lv_obj_t* a = lv_arc_create(disc);
    noFocus(a);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, arcD, arcD);
    lv_obj_center(a);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, arcW, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, col(th.accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_arc_set_angles(a, 0, 60);
    lv_arc_set_rotation(a, i * 180);
    arcs[i] = a;
  }
  discArcA_ = arcs[0];
  discArcB_ = arcs[1];

  // Anh mat dia (an cho den khi dung xong cac khung). Moi khung da cat tron san, nen khong can mat na;
  // vien ve de len tren. Canh khung = long dia (tru vien).
  lv_obj_set_style_border_post(disc, true, 0);
  discD_ = discD - 2 * kDiscBorder;
  discImg_ = lv_img_create(disc);
  noFocus(discImg_);
  lv_obj_add_flag(discImg_, LV_OBJ_FLAG_HIDDEN);

  // Lo giua dia
  const int holeD = std::max(8, discD / 4);
  lv_obj_t* hole = lv_obj_create(disc);
  flatten(hole);
  noFocus(hole);
  lv_obj_set_size(hole, holeD, holeD);
  lv_obj_center(hole);
  lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(hole, col(th.bg), 0);
  lv_obj_set_style_border_width(hole, 2, 0);
  lv_obj_set_style_border_color(hole, col(th.accent), 0);

  // Thanh tien do
  int y = pad + discD + (UI_COMPACT ? 4 : 8);
  bar_ = lv_bar_create(root);
  noFocus(bar_);
  lv_obj_set_size(bar_, discD, UI_COMPACT ? 4 : 6);
  lv_obj_set_pos(bar_, discX, y);
  lv_bar_set_range(bar_, 0, 1000);
  lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar_, col(th.panel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_, col(th.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);

  y += (UI_COMPACT ? 4 : 6) + 4;
  timeLbl_ = lv_label_create(root);
  noFocus(timeLbl_);
  lv_obj_set_width(timeLbl_, discD);
  lv_obj_set_pos(timeLbl_, discX, y);
  lv_obj_set_style_text_align(timeLbl_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(timeLbl_, col(th.text), 0);
  lv_label_set_text(timeLbl_, "--:-- / --:--");

  y += 16;
  ctrlLbl_ = lv_label_create(root);
  noFocus(ctrlLbl_);
  lv_obj_set_width(ctrlLbl_, discD);
  lv_obj_set_pos(ctrlLbl_, discX, y);
  lv_obj_set_style_text_align(ctrlLbl_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(ctrlLbl_, col(th.icon), 0);
  lv_label_set_text(ctrlLbl_, LV_SYMBOL_PREV "   " LV_SYMBOL_PLAY "   " LV_SYMBOL_NEXT);
}

void MusicApp::buildRight(lv_obj_t* root) {
  const Theme& th = Themes::current();
  const int leftW = UI_W / 2;
  const int pad = UI_COMPACT ? 4 : 8;
  const int x = leftW + pad / 2;
  const int w = UI_W - x - pad;

  titleLbl_ = lv_label_create(root);
  noFocus(titleLbl_);
  lv_obj_set_width(titleLbl_, w);
  lv_obj_set_pos(titleLbl_, x, pad);
  lv_label_set_long_mode(titleLbl_, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(titleLbl_, col(th.text), 0);
  lv_label_set_text(titleLbl_, "Chua phat");

  const int infoY = pad + 18;
  // Hang thong tin: trai = so bai (3/12), phai = bieu tuong loa + muc am luong
  infoLbl_ = lv_label_create(root);
  noFocus(infoLbl_);
  lv_obj_set_width(infoLbl_, w / 2);
  lv_obj_set_pos(infoLbl_, x, infoY);
  lv_label_set_long_mode(infoLbl_, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(infoLbl_, col(th.text), 0);
  lv_label_set_text(infoLbl_, "");

  volLbl_ = lv_label_create(root);
  noFocus(volLbl_);
  lv_obj_set_width(volLbl_, w - w / 2);
  lv_obj_set_pos(volLbl_, x + w / 2, infoY);
  lv_obj_set_style_text_align(volLbl_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(volLbl_, col(th.text), 0);
  lv_label_set_text(volLbl_, "");

  // Thanh am luong
  volBar_ = lv_bar_create(root);
  noFocus(volBar_);
  lv_obj_set_size(volBar_, w, 4);
  lv_obj_set_pos(volBar_, x, infoY + 16);
  lv_bar_set_range(volBar_, 0, kVolumeUiMax);
  lv_bar_set_value(volBar_, sVolume, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(volBar_, col(th.panel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(volBar_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(volBar_, col(th.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(volBar_, LV_OPA_COVER, LV_PART_INDICATOR);

  const int listY = infoY + 26;
  list_ = lv_obj_create(root);
  flatten(list_);
  noFocus(list_);
  lv_obj_set_size(list_, w, UI_H - listY - pad);
  lv_obj_set_pos(list_, x, listY);
  lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list_, 0, 0);
}

void MusicApp::rebuildPlaylist() {
  if (!list_) return;
  rows_.clear();
  const int rowH = UI_COMPACT ? 14 : 20;

  if (paths_.empty()) {
    lv_obj_t* l = lv_label_create(list_);
    noFocus(l);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l, col(Themes::current().text), 0);
    lv_label_set_text(l, SdStorage::isMounted() ? "Khong co file .mp3 trong /music" : "Khong doc duoc the SD");
    return;
  }

  for (size_t i = 0; i < names_.size(); i++) {
    lv_obj_t* r = lv_label_create(list_);
    noFocus(r);
    lv_obj_set_size(r, lv_pct(100), rowH);
    lv_label_set_long_mode(r, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_left(r, 3, 0);
    lv_obj_set_style_pad_top(r, 2, 0);
    lv_obj_set_style_radius(r, 3, 0);
    rows_.push_back(r);
    styleRow(i);
  }
  lv_obj_update_layout(list_);
  if (cursor_ >= 0 && cursor_ < (int)rows_.size()) lv_obj_scroll_to_view(rows_[cursor_], LV_ANIM_OFF);
}

void MusicApp::styleRow(size_t i) {
  if (i >= rows_.size() || !rows_[i]) return;
  const Theme& th = Themes::current();
  lv_obj_t* r = rows_[i];
  const bool isCursor = ((int)i == cursor_);
  const bool isPlaying = ((int)i == playingIdx_);

  std::string txt = isPlaying ? (std::string(LV_SYMBOL_PLAY " ") + names_[i]) : names_[i];
  lv_label_set_text(r, txt.c_str());

  if (isCursor) {
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, col(th.panel), 0);
    lv_obj_set_style_text_color(r, col(th.textOnPanel), 0);
  } else {
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(r, col(th.text), 0);
  }
}

void MusicApp::refreshStatus(bool force) {
  if (!alive_ || !timeLbl_) return;
  const AudioState st = AudioService::state();
  const bool active = (st == AudioState::PLAYING || st == AudioState::PAUSED);
  const uint32_t pos = active ? AudioService::positionMs() : 0;
  const uint32_t dur = active ? AudioService::durationMs() : 0;

  const uint32_t sec = pos / 1000;
  if (force || sec != lastShownSec_ || (int)st != lastState_) {
    char a[12], b[12], buf[32];
    fmtTime(a, sizeof(a), pos);
    if (dur > 0) fmtTime(b, sizeof(b), dur);
    else snprintf(b, sizeof(b), "--:--");
    snprintf(buf, sizeof(buf), "%s / %s", a, b);
    lv_label_set_text(timeLbl_, buf);
    lastShownSec_ = sec;
  }
  if (bar_) lv_bar_set_value(bar_, dur > 0 ? (int32_t)((uint64_t)pos * 1000 / dur) : 0, LV_ANIM_OFF);

  if (force || (int)st != lastState_) {
    const char* mid = (st == AudioState::PLAYING) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    char ctrl[48];
    snprintf(ctrl, sizeof(ctrl), "%s   %s   %s", LV_SYMBOL_PREV, mid, LV_SYMBOL_NEXT);
    if (ctrlLbl_) lv_label_set_text(ctrlLbl_, ctrl);

    if (titleLbl_) {
      if (st == AudioState::ERROR) lv_label_set_text(titleLbl_, "Loi doc file");
      else if (active && playingIdx_ >= 0 && playingIdx_ < (int)names_.size())
        lv_label_set_text(titleLbl_, names_[playingIdx_].c_str());
      else lv_label_set_text(titleLbl_, "Chua phat");
    }
    // Neu bai ket thuc hoac dung thi bo dau "dang phat" tren playlist
    if (!active && playingIdx_ >= 0 && st != AudioState::ERROR) {
      int old = playingIdx_;
      playingIdx_ = -1;
      styleRow((size_t)old);
    }
    lastState_ = (int)st;
  }

  if (force) {
    lastVolShown_ = -1;
    lastCursorShown_ = -1;
  }
  if (infoLbl_ && lastCursorShown_ != cursor_) {
    char info[24];
    snprintf(info, sizeof(info), "%d/%d", paths_.empty() ? 0 : cursor_ + 1, (int)paths_.size());
    lv_label_set_text(infoLbl_, info);
    lastCursorShown_ = cursor_;
  }
  if (lastVolShown_ != sVolume) {
    if (volLbl_) {
      char vol[24];
      snprintf(vol, sizeof(vol), "%s %d", LV_SYMBOL_VOLUME_MID, sVolume);
      lv_label_set_text(volLbl_, vol);
    }
    if (volBar_) lv_bar_set_value(volBar_, sVolume, LV_ANIM_OFF);
    lastVolShown_ = sVolume;
  }
}

// ---------------------------------------------------------------- dieu khien
void MusicApp::moveCursor(int delta) {
  const int n = (int)paths_.size();
  if (n == 0) return;
  int old = cursor_;
  cursor_ = ((cursor_ + delta) % n + n) % n;
  styleRow((size_t)old);
  styleRow((size_t)cursor_);
  if (cursor_ < (int)rows_.size()) lv_obj_scroll_to_view(rows_[cursor_], LV_ANIM_OFF);
  refreshStatus(false);
}

void MusicApp::playIndex(int idx) {
  const int n = (int)paths_.size();
  if (n == 0) return;
  idx = ((idx % n) + n) % n;
  int oldPlaying = playingIdx_;
  int oldCursor = cursor_;
  cursor_ = idx;
  playingIdx_ = idx;
  sPlayingPath = paths_[idx];
  AudioService::play(paths_[idx].c_str());
  if (oldPlaying >= 0) styleRow((size_t)oldPlaying);
  styleRow((size_t)oldCursor);
  styleRow((size_t)idx);
  if (idx < (int)rows_.size()) lv_obj_scroll_to_view(rows_[idx], LV_ANIM_OFF);
  lastState_ = -1;
  refreshStatus(true);
}

void MusicApp::stepTrack(int delta) {
  const int n = (int)paths_.size();
  if (n == 0) return;
  int base = (playingIdx_ >= 0) ? playingIdx_ : cursor_;
  playIndex(base + delta);
}

void MusicApp::activateCursor() {
  if (paths_.empty()) return;
  const AudioState st = AudioService::state();
  if (cursor_ == playingIdx_ && st == AudioState::PLAYING) {
    AudioService::pause();
  } else if (cursor_ == playingIdx_ && st == AudioState::PAUSED) {
    AudioService::resume();
  } else {
    playIndex(cursor_);
    return;
  }
  lastState_ = -1;
  refreshStatus(true);
}

void MusicApp::cycleVolume() {
  // 10, 20, ... 100 roi quay lai 10
  sVolume += kVolumeUiStep;
  if (sVolume > kVolumeUiMax) sVolume = kVolumeUiStep;
  applyVolume();
  refreshStatus(false);
}

void MusicApp::handleKey(uint32_t key) {
  switch (key) {
    case LV_KEY_UP: moveCursor(-1); break;
    case LV_KEY_DOWN: moveCursor(+1); break;
    case LV_KEY_LEFT:
    case LV_KEY_RIGHT: {
      const AudioState st = AudioService::state();
      if (st == AudioState::PLAYING || st == AudioState::PAUSED)
        AudioService::seek(key == LV_KEY_LEFT ? -kSeekSeconds : kSeekSeconds);
      break;
    }
    case LV_KEY_ENTER: activateCursor(); break;
    case LV_KEY_ESC: AppManager::goHome(); break;
    default: break;
  }
}

// ---------------------------------------------------------------- callback (co the chay sau onDestroy)
void MusicApp::keyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY) return;
  MusicApp* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
  if (!self || !self->alive_ || !self->root_) return;
  self->handleKey(lv_event_get_key(e));
}

// 9 (NEXT) / 7 (PREV) lam focus nhay sang mot sentinel; ta bat su kien do roi tra focus ve root_ o onTick.
void MusicApp::sinkFocusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
  MusicApp* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
  if (!self || !self->alive_ || !self->armed_ || !self->root_) return;
  lv_obj_t* target = lv_event_get_target(e);
  self->needRefocus_ = true;
  if (target == self->nextSink_) self->stepTrack(+1);
  else if (target == self->prevSink_) self->stepTrack(-1);
}

// ---------------------------------------------------------------- tick (Core 1, khong chan)
void MusicApp::onTick(uint32_t nowMs) {
  if (!alive_ || !root_) return;

  if (needRefocus_) {
    needRefocus_ = false;
    lv_group_focus_obj(root_);
  }

  const AudioState st = AudioService::state();

  if (!coverChecked_ && DiscCover::settled()) applyCover();

  // Tu chuyen bai khi bai ket thuc (chi khi app dang mo)
  if (AudioService::finished() && !paths_.empty()) {
    const int base = (playingIdx_ >= 0) ? playingIdx_ : cursor_;
    playIndex(base + 1);   // het danh sach thi quay ve bai dau; 1 bai thi lap lai
    return;
  }

  // Dia quay khi dang phat
  const uint32_t spinPeriod = hasCover_ ? kCoverFramePeriodMs : kSpinPeriodMs;
  if (st == AudioState::PLAYING && nowMs - lastSpinMs_ >= spinPeriod) {
    lastSpinMs_ = nowMs;
    if (hasCover_ && discImg_) {
      // Doi sang khung xoay dung san ke tiep (khong co phep xoay nao o day)
      const int n = DiscCover::frameCount();
      if (n > 0) {
        frameIdx_ = (frameIdx_ + 1) % n;
        lv_img_set_src(discImg_, DiscCover::frame(frameIdx_));
      }
    } else {
      spinDeg_ = (spinDeg_ + kSpinStepDeg) % 360;
      if (discArcA_) lv_arc_set_rotation(discArcA_, spinDeg_);
      if (discArcB_) lv_arc_set_rotation(discArcB_, (spinDeg_ + 180) % 360);
    }
  }

  if (nowMs - lastUiMs_ >= kUiPeriodMs) {
    lastUiMs_ = nowMs;
    refreshStatus(false);
  }
}

// ---------------------------------------------------------------- factory (pham vi toan cuc)
IApp* createMusicApp() { return new MusicApp(); }