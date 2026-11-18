// Test cap app (Main Screen, Album, Pet + AppManager) tren may tinh.
// MA THAT: core/app_manager, app_registry, theme, input_service, apps/main_screen, apps/album, apps/pet,
//          services/image_service, slideshow_service. LVGL 8.3.11 THAT.
// GIA LAP (stubs/): FreeRTOS (pthread), heap_caps, SD trong bo nho, TJpgDec, TimeService, man hinh, icon.
// Chay duoi ASan + UBSan. Thoi gian (millis) la dong ho ao do test tien.
#include <lvgl.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "Arduino.h"
#include "apps/pet/pet_state.hpp"
#include "config/display.hpp"
#include "config/keymap.hpp"
#include "core/app_manager.hpp"
#include "core/app_registry.hpp"
#include "core/lvgl_port.hpp"
#include "core/theme.hpp"
#include "services/input_service.hpp"
#include "services/slideshow_service.hpp"
#include "test_hooks.hpp"

uint16_t* testFramebuffer();

// ---------------------------------------------------------------- khung test
static int g_checks = 0, g_fail = 0;
static const char* g_test = "";
#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_fail;                                                                \
      if (g_fail <= 40) {                                                      \
        printf("  LOI [%s] %s:%d: %s -- ", g_test, __FILE__, __LINE__, #cond); \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
      }                                                                        \
    }                                                                          \
  } while (0)

static std::mt19937 rng(12345);
static int rnd(int n) { return (int)(rng() % (unsigned)n); }

// ---------------------------------------------------------------- dieu khien
static void step(int ms) {   // mo phong loop() + task LVGL, moi 10 ms
  for (int t = 0; t < ms; t += 10) {
    hooks::nowMs += 10;
    AppManager::tick(hooks::nowMs.load());
    LvglPort::tick();
    usleep(150);
  }
}
static bool waitUntil(const std::function<bool()>& pred, int maxMs = 6000) {
  for (int t = 0; t < maxMs; t += 10) {
    if (pred()) return true;
    step(10);
    usleep(500);
  }
  return pred();
}
static void key(char c, int ms = 30) { Serial.feed(c); step(ms); }
static lv_obj_t* focused() { return lv_group_get_focused(InputService::defaultGroup()); }
static lv_obj_t* scr() { return lv_scr_act(); }
static lv_obj_t* ch(lv_obj_t* o, int i) { return lv_obj_get_child(o, i); }
static bool hid(lv_obj_t* o) { return lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }
static void goApp(AppId id, int ms = 40) { AppManager::goTo(id); step(ms); }
static void goHome(int ms = 40) { AppManager::goHome(); step(ms); }

static long countObjs(lv_obj_t* o) {
  long n = 1;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) n += countObjs(lv_obj_get_child(o, i));
  return n;
}
struct Snap { long bytes, blocks, objs; int tasks; };
static Snap snap() { return {hooks::heapBytes.load(), hooks::heapBlocks.load(), countObjs(scr()), hooks::liveTasks.load()}; }
static void settleTasks(int base) { waitUntil([&] { return hooks::liveTasks.load() <= base; }, 2000); }

// ---------------------------------------------------------------- du lieu
static std::vector<std::string> g_paths;
static void seedPhotos(int n, bool bad = false) {
  hooks::fsClear();
  hooks::fsPutStr("/data/keep.txt", "x");
  g_paths.clear();
  for (int i = 0; i < n; ++i) {
    char p[40]; snprintf(p, sizeof p, "/images/p%03d.jpg", i);
    const int W = 200 + (i * 97) % 3000, H = 150 + (i * 53) % 2000;
    if (bad && i % 5 == 2) hooks::fsPutStr(p, "khong phai jpeg, hong het");
    else hooks::putJpg(p, W, H, i);
    g_paths.push_back(p);
  }
}
static std::string readFile(const char* p) {
  std::vector<uint8_t> v; if (!hooks::fsGet(p, &v)) return "<khong co>";
  return std::string(v.begin(), v.end());
}

// ---------------------------------------------------------------- Main Screen
static lv_obj_t* mainLeft() { return ch(scr(), 0); }
static lv_obj_t* mainGrid() { return ch(scr(), 1); }
static int mainFocus() {   // -1 = widget trai, 0..5 = o luoi, -2 = khong ro
  lv_obj_t* f = focused();
  if (f == mainLeft()) return -1;
  for (int i = 0; i < 6; ++i) if (f == ch(mainGrid(), i)) return i;
  return -2;
}
struct MainModel {
  bool left; int idx; int lastGrid;
  void key(uint32_t k) {
    if (left) {
      const int row = lastGrid / 2;
      if (k == LV_KEY_RIGHT) { left = false; idx = row * 2; }
      else if (k == LV_KEY_LEFT) { left = false; idx = row * 2 + 1; }
      else if (k == LV_KEY_NEXT) { left = false; idx = 0; }
      else if (k == LV_KEY_PREV) { left = false; idx = 5; }
      if (!left) lastGrid = idx;   // su kien FOCUSED cua o luoi cap nhat o nho gan nhat
    } else {
      int row = idx / 2, col = idx % 2;
      if (k == LV_KEY_LEFT) { if (col == 0) { left = true; } else --col; }
      else if (k == LV_KEY_RIGHT) { if (col == 1) { left = true; } else ++col; }
      else if (k == LV_KEY_UP) row = (row + 2) % 3;
      else if (k == LV_KEY_DOWN) row = (row + 1) % 3;
      else if (k == LV_KEY_NEXT) { if (idx == 5) left = true; else ++idx; if (!left) { row = idx / 2; col = idx % 2; } }
      else if (k == LV_KEY_PREV) { if (idx == 0) left = true; else --idx; if (!left) { row = idx / 2; col = idx % 2; } }
      if (!left) { idx = row * 2 + col; lastGrid = idx; }
    }
  }
  int focus() const { return left ? -1 : idx; }
};

static void testMainNav() {
  g_test = "main_nav";
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "phai bat dau o Main");
  CHECK(mainFocus() == 0, "focus ban dau o o 0, dang %d", mainFocus());
  MainModel m{false, 0, 0};
  const char keys[] = {'2', '4', '6', '8', '7', '9'};
  const uint32_t codes[] = {LV_KEY_UP, LV_KEY_LEFT, LV_KEY_RIGHT, LV_KEY_DOWN, LV_KEY_PREV, LV_KEY_NEXT};
  int bad = 0;
  for (int i = 0; i < 4000 && bad < 5; ++i) {
    const int k = rnd(6);
    const MainModel before = m;
    m.key(codes[k]);
    key(keys[k], 30);
    if (getenv("TRACE") && i < 25) printf("   #%d %c: model(left=%d idx=%d lg=%d)->%d  real=%d\n", i, keys[k], before.left, before.idx, before.lastGrid, m.focus(), mainFocus());
    CHECK(mainFocus() == m.focus(), "buoc %d phim %c: focus %d, mo hinh %d", i, keys[k], mainFocus(), m.focus());
    if (mainFocus() != m.focus()) ++bad;
  }
  CHECK(bad == 0, "lech focus");
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "phim dieu huong khong duoc doi app");
  // ESC o Main khong lam gi
  key('1');
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC o Main");
}

static void testMainEnter() {
  g_test = "main_enter";
  const AppId* slots = AppRegistry::launcherSlots();
  for (int i = 0; i < 6; ++i) {
    // dua focus ve o i
    goHome();
    int guard = 0;
    while (mainFocus() != i && guard++ < 12) key('9');
    CHECK(mainFocus() == i, "dua focus toi o %d", i);
    key('5', 50);
    CHECK(AppManager::current() == slots[i], "o %d -> app %d, dang %d", i, (int)slots[i], (int)AppManager::current());
    // phim ESC ve Main, o day phai nho focus
    key('1', 50);
    CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC tu app %d phai ve Main", (int)slots[i]);
    CHECK(mainFocus() == i, "nho focus o %d, dang %d", i, mainFocus());
  }
  // widget trai -> Album, ve lai van o widget trai
  goHome();
  while (mainFocus() != -1) key('9');
  key('5', 50);
  CHECK(AppManager::current() == AppId::APP_ALBUM, "widget trai -> Album");
  key('1', 50);
  CHECK(mainFocus() == -1, "nho focus widget trai");
  // HOME tu tung app
  for (int a = 0; a < (int)AppId::COUNT; ++a) {
    goApp((AppId)a);
    key('h', 50);
    CHECK(AppManager::current() == AppId::MAIN_SCREEN, "HOME tu app %d", a);
  }
}

static void testMainEmptySlots() {
  g_test = "main_empty";
  AppRegistry::setSelected(AppId::APP_MUSIC_PLAYER, false);
  AppRegistry::setSelected(AppId::APP_CALENDAR, false);
  AppRegistry::setSelected(AppId::APP_VISUALIZER, false);
  goApp(AppId::APP_ALBUM); goHome();
  const AppId* slots = AppRegistry::launcherSlots();
  int empties = 0;
  for (int i = 0; i < 6; ++i) if (slots[i] == AppId::COUNT) ++empties;
  CHECK(empties == 3, "so o trong %d", empties);
  for (int i = 0; i < 6; ++i) {
    if (slots[i] != AppId::COUNT) continue;
    goHome();
    int guard = 0;
    while (mainFocus() != i && guard++ < 12) key('9');
    CHECK(mainFocus() == i, "focus duoc o trong %d", i);
    key('5', 60);
    CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ENTER o trong khong lam gi");
    CHECK(lv_obj_get_child_cnt(ch(mainGrid(), i)) == 0, "o trong khong co noi dung");
  }
  AppRegistry::resetSelection();
  goApp(AppId::APP_ALBUM); goHome();
}

static void testMainClock() {
  g_test = "main_clock";
  hooks::tYear = 2026; hooks::tMonth = 11; hooks::tDay = 16; hooks::tHour = 7; hooks::tMin = 5;
  goApp(AppId::APP_ALBUM); goHome();
  lv_obj_t* clk = ch(mainLeft(), 2);
  CHECK(std::string(lv_label_get_text(clk)) == "Nov 16, 07:05", "dong ho: '%s'", lv_label_get_text(clk));
  hooks::tMin = 6;
  step(500);
  CHECK(std::string(lv_label_get_text(clk)) == "Nov 16, 07:05", "chua den 1 s: giu nguyen");
  step(1100);
  CHECK(std::string(lv_label_get_text(clk)) == "Nov 16, 07:06", "doi phut: '%s'", lv_label_get_text(clk));
  const int mons[] = {1, 12, 0, 13};
  const char* names[] = {"Jan", "Dec", "Dec", "Jan"};   // thang 0 va 13 khong duoc crash (ASan)
  for (int i = 0; i < 4; ++i) {
    hooks::tMonth = mons[i]; hooks::tDay = 1; hooks::tHour = 23; hooks::tMin = 59;
    step(1100);
    std::string s = lv_label_get_text(clk);
    CHECK(s.compare(0, 3, names[i]) == 0 || mons[i] == 13, "thang %d -> '%s'", mons[i], s.c_str());
  }
  hooks::tYear = 2099; hooks::tMonth = 12; hooks::tDay = 31; hooks::tHour = 23; hooks::tMin = 59;
  step(1100);
  CHECK(std::string(lv_label_get_text(clk)) == "Dec 31, 23:59", "nam 2099");
  hooks::tYear = 2026; hooks::tMonth = 11; hooks::tDay = 16; hooks::tHour = 7; hooks::tMin = 5;
  step(1100);
}

// ---------------------------------------------------------------- Album
// Cau truc con cua root_: [title, count, 16 o | emptyLbl, viewer, toast]
static lv_obj_t* alRoot() { return ch(scr(), 0); }
static lv_obj_t* alCount() { return ch(alRoot(), 1); }
static lv_obj_t* alViewer() { int n = lv_obj_get_child_cnt(alRoot()); return ch(alRoot(), n - 2); }
static lv_obj_t* alToast() { int n = lv_obj_get_child_cnt(alRoot()); return ch(alRoot(), n - 1); }
static lv_obj_t* alBox(int slot) { return ch(alRoot(), 2 + slot); }
static std::string alHeader() { return lv_label_get_text(alCount()); }
static bool alViewerOpen() { return !hid(alViewer()); }
static lv_obj_t* alViewImg() { return ch(alViewer(), 0); }
static std::string alCaption() { return lv_label_get_text(ch(ch(alViewer(), 2), 0)); }
static std::string alToastText() { return lv_label_get_text(alToast()); }
constexpr int kPer = 16, kColsA = 4;

static int albumNext(int sel, uint32_t k, int n) {
  const int lastRowStart = ((n - 1) / kColsA) * kColsA;
  switch (k) {
    case LV_KEY_LEFT: return (sel + n - 1) % n;
    case LV_KEY_RIGHT: return (sel + 1) % n;
    case LV_KEY_UP:
      if (sel < kColsA) { int x = lastRowStart + sel % kColsA; return x >= n ? n - 1 : x; }
      return sel - kColsA;
    case LV_KEY_DOWN:
      if (sel >= lastRowStart) return sel % kColsA;
      return std::min(sel + kColsA, n - 1);
  }
  return sel;
}
static int parseSel(const std::string& h, int* total = nullptr) {
  int a = 0, b = 0; if (sscanf(h.c_str(), "%d/%d", &a, &b) != 2) return -1;
  if (total) *total = b; return a - 1;
}
static bool thumbsReady(int n, int sel) {
  const int p0 = sel / kPer * kPer;
  for (int s = 0; s < kPer; ++s) {
    lv_obj_t* box = alBox(s);
    if (p0 + s >= n) { if (!hid(box)) return false; continue; }
    if (hid(box)) return false;
    lv_obj_t* img = ch(box, 0); lv_obj_t* mark = ch(box, 1);
    if (hid(img) && (hid(mark) || std::string(lv_label_get_text(mark)) == "...")) return false;
  }
  return true;
}

static void testAlbumNav(int n) {
  static char nm[32]; snprintf(nm, sizeof nm, "album_nav_%d", n); g_test = nm;
  seedPhotos(n);
  goApp(AppId::APP_ALBUM, 50);
  int tot = 0;
  CHECK(parseSel(alHeader(), &tot) == 0, "header '%s' voi %d anh (toi da 256)", alHeader().c_str(), n);
  const int N = std::min(n, 256);
  CHECK(tot == N, "tong %d != %d", tot, N);
  CHECK(waitUntil([&] { return thumbsReady(N, 0); }, 8000), "thumb trang 1 chua xong");
  // thumbnail vua o
  for (int s = 0; s < std::min(N, kPer); ++s) {
    lv_obj_t* img = ch(alBox(s), 0);
    if (hid(img)) continue;
    const lv_img_dsc_t* d = (const lv_img_dsc_t*)lv_img_get_src(img);
    CHECK(d && d->header.w <= lv_obj_get_width(alBox(s)) - 4 && d->header.h <= lv_obj_get_height(alBox(s)) - 4,
          "thumb %d: %dx%d vuot o %dx%d", s, d ? d->header.w : -1, d ? d->header.h : -1, lv_obj_get_width(alBox(s)), lv_obj_get_height(alBox(s)));
  }
  int sel = 0; const char ks[] = {'2', '4', '6', '8'};
  const uint32_t kc[] = {LV_KEY_UP, LV_KEY_LEFT, LV_KEY_RIGHT, LV_KEY_DOWN};
  int bad = 0;
  const int steps = (n <= 40) ? 600 : 300;
  for (int i = 0; i < steps && bad < 5; ++i) {
    const int k = rnd(4);
    sel = albumNext(sel, kc[k], N);
    key(ks[k], 20);
    int s2 = parseSel(alHeader());
    if (s2 != sel) { ++bad; CHECK(false, "buoc %d: header %s, mo hinh %d", i, alHeader().c_str(), sel + 1); }
    // o dang chon co vien focus, dung 1 o
    const int p0 = sel / kPer * kPer;
    int focusCnt = 0;
    for (int s = 0; s < kPer; ++s) {
      if (hid(alBox(s))) continue;
      if (lv_obj_get_style_border_color(alBox(s), 0).full == lv_color_hex(Themes::current().focus).full) {
        ++focusCnt; if (p0 + s != sel) { ++bad; CHECK(false, "vien focus sai o"); }
      }
    }
    if (focusCnt != 1) { ++bad; CHECK(false, "so o co vien focus = %d", focusCnt); }
    int visible = 0; for (int s = 0; s < kPer; ++s) if (!hid(alBox(s))) ++visible;
    if (visible != std::min(kPer, N - p0)) { ++bad; CHECK(false, "so o hien %d, mong %d", visible, std::min(kPer, N - p0)); }
  }
  CHECK(waitUntil([&] { return thumbsReady(N, sel); }, 8000), "thumb trang cuoi chua xong");
  key('1', 50);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC o luoi -> Main");
}

static void testAlbumViewer() {
  g_test = "album_viewer";
  seedPhotos(20);
  goApp(AppId::APP_ALBUM, 50);
  waitUntil([&] { return thumbsReady(20, 0); });
  key('6'); key('6');                              // chon anh 3 (chi so 2)
  key('5', 50);
  CHECK(alViewerOpen(), "ENTER mo viewer");
  CHECK(alCaption().find("p002.jpg") != std::string::npos && alCaption().find("3/20") != std::string::npos, "caption '%s'", alCaption().c_str());
  CHECK(waitUntil([&] { return !hid(alViewImg()); }, 4000), "anh day du chua hien");
  const lv_img_dsc_t* d = (const lv_img_dsc_t*)lv_img_get_src(alViewImg());
  CHECK(d && d->header.w <= UI_W && d->header.h <= UI_H, "anh day du %dx%d", d ? d->header.w : -1, d ? d->header.h : -1);
  // aspect giu nguyen
  { const int W = 200 + (2 * 97) % 3000, H = 150 + (2 * 53) % 2000;
    if (d) CHECK(std::abs((double)d->header.w / d->header.h - (double)W / H) < 0.08, "ti le %dx%d vs %dx%d", d->header.w, d->header.h, W, H); }
  // dieu huong trong viewer: LEFT/UP lui, RIGHT/DOWN tien (quay vong)
  int cur = 2;
  const char ks[] = {'4', '2', '6', '8'}; const int dd[] = {-1, -1, 1, 1};
  for (int i = 0; i < 80; ++i) {
    const int k = rnd(4); cur = (cur + dd[k] + 20) % 20;
    key(ks[k], 20);
    char want[24]; snprintf(want, sizeof want, "%d/20", cur + 1);
    CHECK(alCaption().find(want) != std::string::npos, "buoc %d caption '%s' mong %s", i, alCaption().c_str(), want);
    CHECK(alViewerOpen(), "viewer van mo");
  }
  CHECK(waitUntil([&] { return !hid(alViewImg()); }, 4000), "anh cuoi hien");
  key('1', 50);   // ESC dong viewer, KHONG roi Album
  CHECK(!alViewerOpen() && AppManager::current() == AppId::APP_ALBUM, "ESC dong viewer");
  CHECK(parseSel(alHeader()) == cur, "luoi theo anh vua xem: %s mong %d", alHeader().c_str(), cur + 1);
  key('5', 50); CHECK(alViewerOpen(), "mo lai");
  key('5', 50); CHECK(!alViewerOpen(), "ENTER dong viewer");
  key('1', 50);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC o luoi -> Main");
  // viewer 1 anh: LEFT/RIGHT khong lam gi
  seedPhotos(1);
  goApp(AppId::APP_ALBUM, 50); key('5', 50);
  CHECK(alViewerOpen(), "mo viewer 1 anh");
  key('6'); key('4'); key('2'); key('8');
  CHECK(alCaption().find("1/1") != std::string::npos, "1 anh: '%s'", alCaption().c_str());
  CHECK(waitUntil([&] { return !hid(alViewImg()); }, 4000), "anh 1/1");
  goHome();
}

static void testAlbumOption() {
  g_test = "album_option";
  seedPhotos(8);
  hooks::sdWriteFail = 0;
  goApp(AppId::APP_ALBUM, 50);
  waitUntil([&] { return thumbsReady(8, 0); });
  CHECK(hid(alToast()), "toast an luc dau");
  key('6'); key('6'); key('6');   // anh chi so 3
  key('3', 40);                   // OPTION
  CHECK(!hid(alToast()) && alToastText() == "Set as widget photo", "toast '%s' hid=%d", alToastText().c_str(), (int)hid(alToast()));
  CHECK(readFile("/data/slideshow.txt") == g_paths[3], "file chon: '%s'", readFile("/data/slideshow.txt").c_str());
  step(1300);
  CHECK(!hid(alToast()), "toast con hien sau ~1.3 s");
  step(400);
  CHECK(hid(alToast()), "toast phai tu an sau 1.5 s");
  // OPTION trong viewer lay anh DANG XEM
  key('5', 50); key('6', 30); key('6', 30);   // xem anh 5
  key('3', 40);
  CHECK(readFile("/data/slideshow.txt") == g_paths[5], "viewer: '%s'", readFile("/data/slideshow.txt").c_str());
  CHECK(alToastText() == "Set as widget photo" && !hid(alToast()), "toast trong viewer");
  step(1600);
  // loi ghi SD
  hooks::sdWriteFail = 1;
  key('3', 40);
  CHECK(alToastText() == "Failed to save", "ghi loi: '%s'", alToastText().c_str());
  hooks::sdWriteFail = 0;
  // OPTION 2 lan lien tiep: toast doi, khong hong
  key('3', 40); key('3', 40);
  step(1600);
  CHECK(hid(alToast()), "an toast sau 2 lan");
  key('1', 50); key('1', 50);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ve Main");
  // widget Main hien dung anh da chon (g_paths[5])
  CHECK(waitUntil([&] { return !hid(ch(mainLeft(), 1)); }, 6000), "Main phai hien anh");
  CHECK(hid(ch(mainLeft(), 0)), "chu 'dinhh py' phai an khi co anh");
  const lv_img_dsc_t* d = (const lv_img_dsc_t*)lv_img_get_src(ch(mainLeft(), 1));
  CHECK(d && d->header.w <= 154 && d->header.h <= 232, "anh widget %dx%d", d ? d->header.w : -1, d ? d->header.h : -1);
  // bo anh -> hien chu
  goApp(AppId::APP_ALBUM, 40);
  SlideshowService::clearPhoto();
  goHome(60);
  waitUntil([&] { return SlideshowService::settled(); });
  step(100);
  CHECK(!hid(ch(mainLeft(), 0)) && hid(ch(mainLeft(), 1)), "bo anh -> hien chu");
}

static void testAlbumEmptyAndNoSd() {
  g_test = "album_empty";
  seedPhotos(0);
  goApp(AppId::APP_ALBUM, 60);
  CHECK(alHeader().empty(), "header rong");
  CHECK(std::string(lv_label_get_text(ch(alRoot(), 2))).find("No photos") == 0, "'%s'", lv_label_get_text(ch(alRoot(), 2)));
  const long sw = hooks::sdWrites.load();
  for (char c : std::string("246853")) key(c);            // khong crash, khong doi app
  CHECK(AppManager::current() == AppId::APP_ALBUM, "phim tren Album rong");
  key('5');                                               // ENTER khong mo viewer
  CHECK(!alViewerOpen(), "ENTER o Album rong");
  key('3', 40);                                           // OPTION
  CHECK(hid(alToast()) && hooks::sdWrites.load() == sw, "OPTION o Album rong khong duoc ghi file");
  key('1', 50);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC");
  hooks::sdMounted = 0;
  goApp(AppId::APP_ALBUM, 60);
  CHECK(std::string(lv_label_get_text(ch(alRoot(), 2))) == "Cannot read SD card", "'%s'", lv_label_get_text(ch(alRoot(), 2)));
  key('3', 40);
  key('1', 50);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC khi khong co SD");
  hooks::sdMounted = 1;
}

static void testAlbumBadThumbs() {
  g_test = "album_bad";
  seedPhotos(16, true);   // anh 2, 7, 12 hong
  goApp(AppId::APP_ALBUM, 50);
  CHECK(waitUntil([&] { return thumbsReady(16, 0); }, 8000), "thumb xong");
  for (int i = 0; i < 16; ++i) {
    const bool bad = (i % 5 == 2);
    CHECK(hid(ch(alBox(i), 0)) == bad, "o %d: img hid=%d", i, (int)hid(ch(alBox(i), 0)));
    if (bad) CHECK(std::string(lv_label_get_text(ch(alBox(i), 1))) == "x" && !hid(ch(alBox(i), 1)), "o %d dau x", i);
  }
  key('6'); key('6'); key('5', 50);   // mo anh hong
  CHECK(waitUntil([&] { return std::string(lv_label_get_text(ch(alViewer(), 1))) == "Image error"; }, 4000), "viewer anh hong: '%s'", lv_label_get_text(ch(alViewer(), 1)));
  key('1', 50); key('1', 50);
}

static void testAlbumExitMidLoad() {
  g_test = "album_exit_mid";
  seedPhotos(60);
  goApp(AppId::APP_ALBUM, 20); goHome(60);   // khoi dong nguoi
  settleTasks(0);
  const Snap b = snap();
  hooks::decodeDelayMs = 15;
  for (int i = 0; i < 80; ++i) {
    goApp(AppId::APP_ALBUM, 10);
    const int w = rnd(120);
    step(w - w % 10);
    if (rnd(3) == 0) { key('5', 10); step(rnd(4) * 10); }      // mo viewer giua chung
    if (rnd(4) == 0) { for (int k = 0; k < 6; ++k) { Serial.feed('6'); } step(20); }
    goHome(10);
  }
  hooks::decodeDelayMs = 0;
  step(200);
  settleTasks(b.tasks);
  const Snap a = snap();
  CHECK(a.tasks == b.tasks, "task con song %d -> %d", b.tasks, a.tasks);
  CHECK(a.bytes == b.bytes && a.blocks == b.blocks, "ro ri bo nho: %ld B / %ld khoi -> %ld B / %ld khoi", b.bytes, b.blocks, a.bytes, a.blocks);
  CHECK(a.objs == b.objs, "object LVGL %ld -> %ld", b.objs, a.objs);
}

static void testAlbumViewerRace() {
  g_test = "album_viewer_race";
  seedPhotos(30);
  // anh chan ngang, anh le doc: ket qua cu (anh ke ben) se sai ti le ro ret
  for (int i = 0; i < 30; ++i) hooks::putJpg(g_paths[i].c_str(), i % 2 ? 1800 : 2400, i % 2 ? 2400 : 1800, i);
  goApp(AppId::APP_ALBUM, 50);
  waitUntil([&] { return thumbsReady(16, 0); });
  hooks::decodeDelayMs = 25;
  key('5', 10);
  int shownBad = 0, shownCnt = 0; const long dc0 = hooks::decodeCalls.load();
  for (int i = 0; i < 25; ++i) {
    Serial.feed('6'); usleep(12000);   // 12 ms that: nhanh hon 1 lan giai ma (25 ms)
    // Nua dau cua mot vong lap: AppManager::tick nhan ket qua tu task nen -> kiem tra NGAY, truoc khi phim moi duoc xu ly
    hooks::nowMs += 10; AppManager::tick(hooks::nowMs.load());
    if (!hid(alViewImg())) {   // anh dang hien phai la anh cua chi so DANG XEM (ket qua cu khong duoc hien)
      const std::string capS = alCaption(); const size_t sp = capS.rfind(' ');
      const int cur = sp == std::string::npos ? -1 : atoi(capS.c_str() + sp + 1) - 1;
      const lv_img_dsc_t* d0 = (const lv_img_dsc_t*)lv_img_get_src(alViewImg());
      const int W0 = cur % 2 ? 1800 : 2400, H0 = cur % 2 ? 2400 : 1800;
      ++shownCnt;
      if (d0 && std::abs((double)d0->header.w / d0->header.h - (double)W0 / H0) > 0.08) ++shownBad;
    }
    LvglPort::tick();
  }
  printf("  [race] giai ma %ld lan, hien anh %d lan\n", hooks::decodeCalls.load() - dc0, shownCnt);
  CHECK(shownBad == 0, "co %d lan hien anh cua CHI SO CU trong khi dang xem anh khac", shownBad);
  hooks::decodeDelayMs = 0;
  int want = 25 % 30;
  char w[16]; snprintf(w, sizeof w, "%d/30", want + 1);
  CHECK(alCaption().find(w) != std::string::npos, "caption '%s' mong %s", alCaption().c_str(), w);
  CHECK(waitUntil([&] { return !hid(alViewImg()); }, 8000), "anh cuoi phai hien");
  // anh hien phai dung anh cuoi: kiem tra ti le
  const lv_img_dsc_t* d = (const lv_img_dsc_t*)lv_img_get_src(alViewImg());
  const int W = want % 2 ? 1800 : 2400, H = want % 2 ? 2400 : 1800;
  step(300);
  d = (const lv_img_dsc_t*)lv_img_get_src(alViewImg());
  CHECK(d && std::abs((double)d->header.w / d->header.h - (double)W / H) < 0.08, "anh KHONG khop anh cuoi: %dx%d mong ti le %dx%d", d ? d->header.w : -1, d ? d->header.h : -1, W, H);
  goHome(60);
}

// ---------------------------------------------------------------- Pet
static lv_obj_t* petRoot() { return ch(scr(), 0); }
// 6 nut = cac con lv_btn cua root, theo thu tu tao. Tim bang lv_obj_check_type la nut.
static lv_obj_t* petBtn(int i) {
  int k = 0;
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(petRoot()); ++c) {
    lv_obj_t* o = ch(petRoot(), c);
    if (lv_obj_check_type(o, &lv_btn_class)) { if (k == i) return o; ++k; }
  }
  return nullptr;
}
static int petFocus() { lv_obj_t* f = focused(); for (int i = 0; i < 6; ++i) if (petBtn(i) == f) return i; return -1; }
static void petGoto(int i) {
  int guard = 0;
  while (petFocus() != i && guard++ < 10) key('9', 20);
}

static void testPetNav() {
  g_test = "pet_nav";
  goApp(AppId::APP_PET, 60);
  CHECK(petBtn(5) != nullptr && petBtn(6) == nullptr, "dung 6 nut");
  CHECK(petFocus() >= 0, "co focus");
  int idx = petFocus();
  const char ks[] = {'2', '8', '4', '6', '7', '9'};
  int bad = 0;
  for (int i = 0; i < 1500 && bad < 5; ++i) {
    const int k = rnd(6);
    int row = idx % 3, col = idx / 3;
    switch (k) {
      case 0: row = (row + 2) % 3; break;
      case 1: row = (row + 1) % 3; break;
      case 2: case 3: col = (col + 1) % 2; break;
      case 4: idx = (idx + 5) % 6; row = idx % 3; col = idx / 3; break;
      case 5: idx = (idx + 1) % 6; row = idx % 3; col = idx / 3; break;
    }
    idx = col * 3 + row;
    key(ks[k], 20);
    if (petFocus() != idx) { ++bad; CHECK(false, "buoc %d phim %c: focus %d, mo hinh %d", i, ks[k], petFocus(), idx); }
  }
  CHECK(AppManager::current() == AppId::APP_PET, "van o Pet");
  // nho focus khi vao lai
  petGoto(4);
  key('1', 50);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC ve Main");
  goApp(AppId::APP_PET, 60);
  CHECK(petFocus() == 4, "nho nut 4, dang %d", petFocus());
  petGoto(2);
  goHome(); goApp(AppId::APP_PET, 60);
  CHECK(petFocus() == 2, "nho nut 2 sau khi goHome, dang %d", petFocus());
  goHome();
}

static void testPetActions() {
  g_test = "pet_actions";
  PetState& st = PetState::instance();
  goApp(AppId::APP_PET, 60);
  // dua trang thai ve moc ro rang: dung chi so hien tai, tinh delta (chi so giam theo thoi gian nen doc truoc/sau trong cung 1 tick)
  auto stat = [&](PetStat s) { return (int)st.value(s); };
  CHECK(!st.sleeping(), "dang thuc");
  petGoto(0);
  int h0 = stat(PetStat::HUNGER), c0 = stat(PetStat::CLEAN);
  key('5', 10);
  CHECK(stat(PetStat::HUNGER) >= std::min(100, h0 + 30 - 2) && stat(PetStat::HUNGER) <= std::min(100, h0 + 30), "Eat: no %d -> %d", h0, stat(PetStat::HUNGER));
  CHECK(stat(PetStat::CLEAN) <= c0 - 5 + 1 && stat(PetStat::CLEAN) >= c0 - 6, "Eat: sach %d -> %d", c0, stat(PetStat::CLEAN));
  CHECK(st.mood() == PetMood::HAPPY, "vui sau khi an");
  petGoto(1);
  int f0 = stat(PetStat::FUN); h0 = stat(PetStat::HUNGER);
  key('5', 10);
  CHECK(stat(PetStat::FUN) >= std::min(100, f0 + 25) - 1 && stat(PetStat::FUN) <= std::min(100, f0 + 25), "Play: vui %d -> %d", f0, stat(PetStat::FUN));
  CHECK(stat(PetStat::HUNGER) <= h0 - 8 + 1, "Play: no giam %d -> %d", h0, stat(PetStat::HUNGER));
  petGoto(2);
  c0 = stat(PetStat::CLEAN);
  key('5', 10);
  CHECK(stat(PetStat::CLEAN) >= std::min(100, c0 + 40) - 1 && stat(PetStat::CLEAN) <= 100, "Bath: sach %d -> %d", c0, stat(PetStat::CLEAN));
  // clamp 100
  for (int i = 0; i < 6; ++i) { petGoto(0); key('5', 10); }
  CHECK(stat(PetStat::HUNGER) <= 100, "khong vuot 100");
  // skin xoay vong
  petGoto(4);
  const int s0 = (int)st.skin();
  for (int i = 1; i <= 5; ++i) { key('5', 20); CHECK((int)st.skin() == (s0 + i) % (int)PetSkin::COUNT, "skin buoc %d", i); }
  // nut trong: khong doi gi
  petGoto(5);
  const int e0 = stat(PetStat::ENERGY);
  key('5', 10);
  CHECK(stat(PetStat::ENERGY) >= e0 - 1 && (int)st.skin() == (s0 + 5) % (int)PetSkin::COUNT, "nut trong");
  // Sleep: chua buon ngu neu nang luong day
  petGoto(3);
  if (stat(PetStat::ENERGY) >= 100) { key('5', 10); CHECK(!st.sleeping(), "nang luong day: khong ngu"); }
  // ep nang luong giam: tien thoi gian 30 s (nhanh: 10 s moi diem)
  step(30000);
  CHECK(stat(PetStat::ENERGY) < 100, "nang luong phai giam theo thoi gian (%d)", stat(PetStat::ENERGY));
  petGoto(3); key('5', 10);
  CHECK(st.sleeping(), "bam Sleep -> ngu");
  CHECK(st.mood() == PetMood::SLEEPING, "mood ngu");
  const int h1 = stat(PetStat::HUNGER);
  petGoto(0); key('5', 10);
  CHECK(stat(PetStat::HUNGER) <= h1, "dang ngu: khong an duoc");
  petGoto(1); key('5', 10); petGoto(2); key('5', 10);
  CHECK(st.sleeping(), "van ngu");
  // vao/ra app luc dang ngu; hoi nang luong; tu day
  goHome(); step(5000); goApp(AppId::APP_PET, 60);
  CHECK(st.sleeping() || stat(PetStat::ENERGY) > 0, "trang thai giu qua lan thoat app");
  petGoto(3); key('5', 10);
  CHECK(!st.sleeping(), "bam Sleep lai -> day");
  // ngu cho toi khi nang luong day (tu day), roi bam Sleep: chua buon ngu -> khong ngu
  petGoto(3); key('5', 10);
  if (!st.sleeping()) { petGoto(3); key('5', 10); }
  CHECK(st.sleeping(), "dua ve trang thai ngu");
  hooks::nowMs += 400000; step(20);       // 1 tick lon: hoi day nang luong, tu thuc day
  CHECK(!st.sleeping() && st.value(PetStat::ENERGY) == 100, "nang luong day thi tu day (ngu=%d nl=%d)", (int)st.sleeping(), (int)st.value(PetStat::ENERGY));
  petGoto(3); key('5', 10);
  CHECK(!st.sleeping(), "nang luong day: bam Sleep khong duoc ngu");
  // doi tuong chi so ngoai pham vi
  for (int s = 0; s < 4; ++s) CHECK(stat((PetStat)s) >= 0 && stat((PetStat)s) <= 100, "chi so %d ngoai [0,100]", s);
  goHome();
}

// ---------------------------------------------------------------- Ro ri / vong vao ra
static void cycleApp(AppId a) {
  goApp(a, 30);
  if (a == AppId::APP_ALBUM) { step(rnd(5) * 10); if (rnd(2)) key('5', 10); }
  if (a == AppId::APP_PET) { key('5', 10); key('6', 10); }
  goHome(30);
}

static void testLeaks() {
  g_test = "leaks";
  seedPhotos(25);
  hooks::decodeDelayMs = 0;
  const AppId apps[] = {AppId::APP_ALBUM, AppId::APP_PET, AppId::APP_MUSIC_PLAYER, AppId::APP_SETTINGS, AppId::APP_CALENDAR, AppId::APP_VISUALIZER};
  for (int w = 0; w < 3; ++w) for (AppId a : apps) cycleApp(a);   // khoi dong nguoi
  waitUntil([&] { return SlideshowService::settled(); });
  settleTasks(0);
  const Snap base = snap();
  printf("  [leaks] baseline: %ld B, %ld khoi, %ld obj, %d task\n", base.bytes, base.blocks, base.objs, base.tasks);
  const int N = 300;
  std::vector<long> series;
  for (int i = 0; i < N; ++i) {
    cycleApp(apps[i % 6]);
    if (i % 50 == 49) { settleTasks(base.tasks); series.push_back(hooks::heapBytes.load() - base.bytes); }
  }
  settleTasks(base.tasks);
  const Snap a = snap();
  printf("  [leaks] sau %d vong: %ld B (%+ld), %ld khoi (%+ld), %ld obj, %d task\n", N, a.bytes, a.bytes - base.bytes, a.blocks, a.blocks - base.blocks, a.objs, a.tasks);
  CHECK(a.bytes == base.bytes, "ro ri %+ld B", a.bytes - base.bytes);
  CHECK(a.blocks == base.blocks, "ro ri %+ld khoi", a.blocks - base.blocks);
  CHECK(a.objs == base.objs, "object LVGL %ld -> %ld", base.objs, a.objs);
  CHECK(a.tasks == base.tasks, "task %d -> %d", base.tasks, a.tasks);
  for (size_t i = 0; i < series.size(); ++i) CHECK(series[i] == 0, "diem do %zu: %+ld B", i, series[i]);
  CHECK(hooks::lockViolations.load() == 0, "vi pham khoa");
}

// Chen loi het PSRAM o moi vi tri: app van chay, khong crash, khong ro ri.
static void testPsramFailures() {
  g_test = "psram_fail";
  seedPhotos(25);
  goApp(AppId::APP_ALBUM, 40); key('3', 20); goHome(80);   // co anh widget truoc khi lay moc
  waitUntil([&] { return SlideshowService::settled(); });
  const AppId apps[] = {AppId::MAIN_SCREEN, AppId::APP_ALBUM, AppId::APP_PET};
  for (AppId a : apps) {
    goHome(); goApp(a, 30); goHome(60); settleTasks(0);
    waitUntil([&] { return SlideshowService::settled(); });
    const Snap base = snap();
    long maxN = 0;
    for (long n = 0; n < 60; ++n) {
      if (getenv("TRACE")) { printf("   app %d n=%ld\n", (int)a, n); fflush(stdout); }
      hooks::failAfter = n; hooks::failCount = 0;
      goHome(10);
      if (a == AppId::MAIN_SCREEN) { goApp(AppId::APP_ALBUM, 20); key('3', 10); goHome(60); }   // main phai chay duoc cung loi
      else goApp(a, 40);
      step(120);
      if (a == AppId::APP_ALBUM) { key('5', 10); step(80); key('6', 10); step(60); }
      if (a == AppId::APP_PET) { for (char c : std::string("5658")) key(c, 10); step(80); }
      const bool hit = hooks::failCount.load() > 0;
      hooks::failAfter = -1;
      goHome(60);
      waitUntil([&] { return SlideshowService::settled(); }, 3000);
      CHECK(AppManager::current() == AppId::MAIN_SCREEN, "app %d, n=%ld: khong ve duoc Main", (int)a, n);
      if (hit) maxN = n;
      else if (n > maxN + 3) break;
    }
    hooks::failAfter = -1;
    settleTasks(base.tasks);
    // nap lai anh slideshow: de so sanh cong bang, dua Main ve dung trang thai co anh
    goApp(AppId::APP_ALBUM, 30); goHome(60);
    waitUntil([&] { return SlideshowService::settled(); });
    settleTasks(base.tasks);
    const Snap a2 = snap();
    printf("  [psram_fail] app %d: chen loi toi vi tri %ld; sau do %+ld B, %+ld khoi, %d task\n", (int)a, maxN, a2.bytes - base.bytes, a2.blocks - base.blocks, a2.tasks);
    CHECK(a2.bytes <= base.bytes + 0, "app %d ro ri %+ld B sau khi het PSRAM", (int)a, a2.bytes - base.bytes);
    CHECK(a2.tasks == base.tasks, "task %d -> %d", base.tasks, a2.tasks);
  }
  // tao task that bai (Album)
  hooks::taskCreateFail = 1;
  goApp(AppId::APP_ALBUM, 80); key('5', 20); step(100); key('1', 20); key('1', 40);
  hooks::taskCreateFail = 0;
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "Album chay duoc khi khong tao duoc task");
}

// ---------------------------------------------------------------- AppManager
static void testManager() {
  g_test = "manager";
  goHome();
  // goTo chinh app dang chay: bo qua (khong tao lai)
  const long created0 = hooks::heapAllocs.load();
  AppManager::goTo(AppId::MAIN_SCREEN); step(50);
  CHECK(hooks::heapAllocs.load() - created0 < 20, "goTo chinh minh khong duoc dung lai man hinh (%ld cap phat)", hooks::heapAllocs.load() - created0);
  AppManager::goTo(AppId::COUNT); step(30);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "goTo(COUNT) bo qua");
  AppManager::goTo((AppId)200); step(30);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "goTo(200) bo qua");
  // hai goTo trong 1 tick: cai sau thang
  AppManager::goTo(AppId::APP_PET); AppManager::goTo(AppId::APP_ALBUM); step(30);
  CHECK(AppManager::current() == AppId::APP_ALBUM, "goTo cuoi thang");
  // goTo + reload cung tick: chuyen app (man hinh moi dung theme moi)
  AppManager::goTo(AppId::APP_PET); AppManager::reload(); step(30);
  CHECK(AppManager::current() == AppId::APP_PET, "goTo + reload");
  // reload dung lai man hinh va ap theme
  for (size_t t = 0; t < Themes::count(); ++t) {
    Themes::select(Themes::at(t).key);
    AppManager::reload(); step(30);
    CHECK(AppManager::current() == AppId::APP_PET, "reload giu app");
  }
  Themes::select("pink");
  AppManager::reload(); step(30);
  goHome();
  // tran hang doi phim: gui 100 OPTION khong tick -> khong crash, app nhan toi da 8
  for (int i = 0; i < 100; ++i) AppManager::postKey(KEY_OPTION);
  step(30);
  // phim lung tung khong thuoc keymap
  for (char c : std::string("0abcxyz!@ \n\t")) key(c, 10);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ky tu la khong doi app");
  // app chua cai: ESC ve Main
  goApp(AppId::APP_CALENDAR, 40);
  CHECK(AppManager::current() == AppId::APP_CALENDAR, "Calendar (chua cai) mo duoc");
  key('1', 40);
  CHECK(AppManager::current() == AppId::MAIN_SCREEN, "ESC tu man hinh thay the");
}

// Mau theme + khong ve ra ngoai man hinh.
static uint16_t px(int x, int y) { return testFramebuffer()[y * UI_W + x]; }
static void testThemes() {
  g_test = "themes";
  seedPhotos(10);
  goApp(AppId::APP_ALBUM, 40); key('3', 20); goHome(80);
  for (size_t t = 0; t < Themes::count(); ++t) {
    const Theme& th = Themes::at(t);
    Themes::select(th.key);
    const long bad0 = hooks::flushBadArea.load();
    goApp(AppId::APP_PET, 60); lv_refr_now(nullptr);
    goApp(AppId::APP_ALBUM, 60); waitUntil([&] { return thumbsReady(10, 0); }); key('5', 30); step(200); lv_refr_now(nullptr);
    key('1', 30); goHome(80); step(100); lv_refr_now(nullptr);
    CHECK(AppManager::current() == AppId::MAIN_SCREEN, "theme %s", th.key);
    // khoang giua widget trai (x<=158) va luoi (x>=162): mau nen man hinh cua theme
    CHECK(px(160, 100) == lv_color_hex(th.bg).full, "theme %s: nen %04x mong %04x (co hex cung?)", th.key, px(160, 100), lv_color_hex(th.bg).full);
    // nut luoi: lay diem sat mep trai, giua chieu cao (khong dinh icon)
    lv_obj_t* b0 = ch(mainGrid(), 0);
    lv_area_t a; lv_obj_get_coords(b0, &a);
    const int sx = a.x1 + 4, sy = (a.y1 + a.y2) / 2;
    CHECK(px(sx, sy) == lv_color_hex(th.panel).full, "theme %s: nut %04x mong panel %04x", th.key, px(sx, sy), lv_color_hex(th.panel).full);
    // icon bo theo khoa
    const int set = !strcmp(th.key, "mint") ? 1 : !strcmp(th.key, "pink") ? 2 : 0;
    lv_obj_t* img = ch(b0, 0);
    if (!UI_COMPACT) CHECK(lv_obj_check_type(img, &lv_img_class) && lv_img_get_src(img) == AppRegistry::icon(AppRegistry::launcherSlots()[0], set), "theme %s: icon bo %d", th.key, set);
    CHECK(hooks::flushBadArea.load() == bad0, "theme %s: ve ra ngoai man hinh", th.key);
    // doi theme roi reload() tai cho: man hinh dang mo phai doi mau ngay
    const Theme& nx = Themes::at((t + 1) % Themes::count());
    Themes::select(nx.key);
    AppManager::reload(); step(60); lv_refr_now(nullptr);
    CHECK(px(160, 100) == lv_color_hex(nx.bg).full, "reload: nen %04x mong %04x (theme %s)", px(160, 100), lv_color_hex(nx.bg).full, nx.key);
    Themes::select(th.key);
    AppManager::reload(); step(60);
  }
  Themes::select("pink");
  goApp(AppId::APP_ALBUM, 30); goHome(80);
}

// ---------------------------------------------------------------- Fuzz toan he thong
static void testFuzz() {
  g_test = "fuzz";
  seedPhotos(40, true);
  goApp(AppId::APP_ALBUM, 30); SlideshowService::setPhoto(g_paths[0].c_str()); goHome(60);
  waitUntil([&] { return SlideshowService::settled(); });
  settleTasks(0);
  const Snap base = snap();
  const std::string alphabet = "12345678923456h";
  long maxObjs = 0; int nApp[8] = {};
  for (int i = 0; i < 20000; ++i) {
    const char c = alphabet[rnd((int)alphabet.size())];
    Serial.feed(c);
    if (rnd(8) == 0) Serial.feed(alphabet[rnd((int)alphabet.size())]);
    step(10 + rnd(5) * 10);
    if (rnd(900) == 0) { Themes::select(Themes::at(rnd((int)Themes::count())).key); AppManager::reload(); }
    if (rnd(700) == 0) { hooks::decodeDelayMs = rnd(2) ? 12 : 0; }
    if (rnd(1500) == 0) { hooks::sdMounted = 0; }
    if (rnd(300) == 0) { hooks::sdMounted = 1; }
    const AppId cur = AppManager::current();
    if ((int)cur < 8) ++nApp[(int)cur];
    lv_obj_t* f = focused();
    if (f) {
      lv_obj_t* p = f; while (lv_obj_get_parent(p)) p = lv_obj_get_parent(p);
      if (p != scr()) { CHECK(false, "buoc %d: focus thuoc man hinh khac (app %d)", i, (int)cur); break; }
    }
    if (i % 500 == 0) maxObjs = std::max(maxObjs, countObjs(scr()));
    if (g_fail > 10) break;
  }
  hooks::decodeDelayMs = 0; hooks::sdMounted = 1;
  Themes::select("pink");
  printf("  [fuzz] thoi gian o moi app: ");
  for (int i = 0; i < 7; ++i) printf("%d=%d ", i, nApp[i]);
  printf(" | max object %ld\n", maxObjs);
  for (int i : {1, 2, 3, 5, 6}) if (i != 3) CHECK(nApp[i] > 0, "fuzz chua tung vao app %d", i);
  goHome(80); step(500); waitUntil([&] { return SlideshowService::settled(); });
  settleTasks(base.tasks);
  // fuzz co the da doi anh widget (OPTION trong Album): dat lai dung anh luc dau de so sanh
  goApp(AppId::APP_ALBUM, 30); SlideshowService::setPhoto(g_paths[0].c_str()); goHome(80);
  waitUntil([&] { return SlideshowService::settled(); }); step(100); settleTasks(base.tasks);
  const Snap a = snap();
  printf("  [fuzz] sau fuzz: %+ld B, %+ld khoi, %ld obj (goc %ld), %d task\n", a.bytes - base.bytes, a.blocks - base.blocks, a.objs, base.objs, a.tasks);
  CHECK(a.tasks == base.tasks, "task %d -> %d", base.tasks, a.tasks);
  CHECK(a.objs == base.objs, "object %ld -> %ld", base.objs, a.objs);
  CHECK(std::labs(a.bytes - base.bytes) <= 512 * 2, "bo nho lech %+ld B sau fuzz", a.bytes - base.bytes);
}

// millis() tran so
static void testMillisWrap() {
  g_test = "millis_wrap";
  seedPhotos(6);
  hooks::nowMs = 0xFFFFFF00u - 400;
  goApp(AppId::APP_ALBUM, 50); waitUntil([&] { return thumbsReady(6, 0); });
  key('3', 40);     // toast se het han SAU khi millis tran
  CHECK(!hid(alToast()), "toast hien");
  step(600);
  CHECK(!hid(alToast()), "toast chua het han (t+600ms) mil=%u", hooks::nowMs.load());
  step(1200);
  CHECK(hid(alToast()), "toast phai an sau khi millis tran, mil=%u", hooks::nowMs.load());
  CHECK(hooks::nowMs.load() < 30000, "dong ho ao da tran (%u)", hooks::nowMs.load());
  PetState& st = PetState::instance();
  hooks::nowMs = 0xFFFFFF00u - 1000;
  goApp(AppId::APP_PET, 50);            // lan nhay thoi gian nhan tao duoc PetState hap thu o day
  // dua tinh trang ve "vua cho an" de khong bi ngu / dung 0
  if (st.sleeping()) { petGoto(3); key('5', 10); }   // nhay thoi gian lam nang luong ve 0 -> dang ngu: danh thuc truoc
  petGoto(0); key('5', 10); petGoto(2); key('5', 10);
  CHECK(!st.sleeping() && st.value(PetStat::HUNGER) >= 20, "chuan bi Pet: ngu=%d no=%d", (int)st.sleeping(), (int)st.value(PetStat::HUNGER));
  const int e0 = st.value(PetStat::ENERGY), h0 = st.value(PetStat::HUNGER);
  step(3000);                           // truoc diem tran
  step(5000);                           // qua diem tran
  CHECK(hooks::nowMs.load() < 30000, "dong ho ao da tran (%u)", hooks::nowMs.load());
  CHECK(h0 - (int)st.value(PetStat::HUNGER) >= 1 && h0 - (int)st.value(PetStat::HUNGER) <= 2, "Pet: no %d -> %d qua diem tran (8 s thuc ra chi mat ~1 diem)", h0, st.value(PetStat::HUNGER));
  CHECK(e0 - (int)st.value(PetStat::ENERGY) <= 2 && (int)st.value(PetStat::ENERGY) <= e0 + 3, "Pet: nang luong %d -> %d", e0, st.value(PetStat::ENERGY));
  // roi Pet roi vao lai sau khi millis() da tran: phai tinh du thoi gian da troi qua (khong mat)
  goApp(AppId::APP_PET, 30);
  hooks::nowMs = 0xFFFFFF00u - 3000; step(20);       // Pet dang mo: nhay thoi gian duoc hap thu ngay
  if (st.sleeping()) { petGoto(3); key('5', 10); }
  petGoto(0); key('5', 10); petGoto(1); key('5', 10); petGoto(2); key('5', 10);
  goHome(20);
  const int h1 = st.value(PetStat::HUNGER);
  step(8000);                            // tran trong luc o Main
  goApp(AppId::APP_PET, 30);
  CHECK(h1 - (int)st.value(PetStat::HUNGER) >= 1 && h1 - (int)st.value(PetStat::HUNGER) <= 2, "Pet vang mat qua diem tran: no %d -> %d", h1, st.value(PetStat::HUNGER));
  goHome(60);
}

int main(int argc, char** argv) {
  const char* only = argc > 1 ? argv[1] : nullptr;
  LvglPort::init();
  { LvglLock l; InputService::init(); AppManager::init(); }
  setvbuf(stdout, nullptr, _IOLBF, 0);
  seedPhotos(6);
  step(100);
  struct T { const char* n; std::function<void()> f; };
  std::vector<T> tests = {
      {"main_nav", testMainNav}, {"main_enter", testMainEnter}, {"main_empty", testMainEmptySlots}, {"main_clock", testMainClock},
      {"album_nav", [] { for (int n : {1, 5, 16, 17, 40, 300}) testAlbumNav(n); }},
      {"album_viewer", testAlbumViewer}, {"album_option", testAlbumOption}, {"album_empty", testAlbumEmptyAndNoSd},
      {"album_bad", testAlbumBadThumbs}, {"album_exit_mid", testAlbumExitMidLoad}, {"album_race", testAlbumViewerRace},
      {"pet_nav", testPetNav}, {"pet_actions", testPetActions}, {"themes", testThemes}, {"manager", testManager},
      {"millis_wrap", testMillisWrap}, {"leaks", testLeaks}, {"psram_fail", testPsramFailures}, {"fuzz", testFuzz},
  };
  for (auto& t : tests) {
    if (only && strcmp(only, t.n) != 0) continue;
    const int f0 = g_fail, c0 = g_checks;
    printf("[%s]\n", t.n);
    t.f();
    printf("  -> %d kiem tra, %d loi\n", g_checks - c0, g_fail - f0);
  }
  CHECK(hooks::lockViolations.load() == 0, "vi pham khoa");
  CHECK(hooks::flushBadArea.load() == 0, "ve ra ngoai man hinh %ld lan", hooks::flushBadArea.load());
  printf("== %d kiem tra, %d loi ==\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
