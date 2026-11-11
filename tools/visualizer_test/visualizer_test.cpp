// Test máy tính cho VisualizerApp, dùng STUB LVGL (tools/visualizer_test/stubs), không phải LVGL thật.
// Kiểm tra logic của app: bố cục, phím 4/6/7/9, ESC, callback sau onDestroy, rò rỉ object, nhiễu ngẫu nhiên.
// Không kiểm tra được: vẽ thật, tốc độ, hành vi LVGL ngoài những gì stub mô phỏng (ghi rõ trong ARCHITECTURE.md).
//
// Chạy từ thư mục gốc dự án (Windows MSYS2 UCRT64 chạy giống hệt, đổi ./vis_test thành ./vis_test.exe):
//   g++ -std=gnu++11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Itools/visualizer_test/stubs -Isrc tools/visualizer_test/visualizer_test.cpp tools/visualizer_test/stubs/lvgl_stub.cpp src/apps/visualizer/visualizer_app.cpp src/apps/visualizer/fft.cpp -o vis_test && ./vis_test
// Thêm -DSCREEN_W=160 -DSCREEN_H=128 để thử màn nhỏ.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "apps/visualizer/visualizer_app.hpp"
#include "config/display.hpp"
#include "core/app_manager.hpp"
#include "core/theme.hpp"
#include "services/audio_service.hpp"
#include "services/input_service.hpp"

// ---------------- Stub dịch vụ do test điều khiển ----------------
static int g_goHome = 0;
static AudioState g_state = AudioState::STOPPED;
static int16_t g_pcm[256];
static size_t g_pcmCount = 0;
static int g_readCalls = 0;
static lv_group_t g_group;
static Theme g_theme = {"t", "T", 0xF8DCE6, 0xF2A7C3, 0xE0408A, 0x402030, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x000000, 0, nullptr};

namespace AppManager {
void goHome() { ++g_goHome; }
void goTo(AppId) {}
}  // namespace AppManager
namespace AudioService {
AudioState state() { return g_state; }
size_t readPcmTap(int16_t* dst, size_t n) {
  ++g_readCalls;
  size_t c = g_pcmCount < n ? g_pcmCount : n;
  for (size_t i = 0; i < c; ++i) dst[i] = g_pcm[i];
  return c;
}
}  // namespace AudioService
namespace InputService {
lv_group_t* defaultGroup() { return &g_group; }
}  // namespace InputService
namespace Themes {
const Theme& current() { return g_theme; }
}  // namespace Themes

// ---------------- Khung test ----------------
static int gChecks = 0, gFails = 0;
#define CHECK(cond, ...)                                      \
  do {                                                        \
    ++gChecks;                                                \
    if (!(cond)) {                                            \
      ++gFails;                                               \
      std::printf("  FAIL line %d: %s  ", __LINE__, #cond);   \
      std::printf(__VA_ARGS__);                               \
      std::printf("\n");                                      \
    }                                                         \
  } while (0)

static const double kPi = 3.14159265358979323846;

static void setSine(double bin, double amp) {
  for (int i = 0; i < 256; ++i) g_pcm[i] = static_cast<int16_t>(std::lround(amp * 32767.0 * std::sin(2 * kPi * bin * i / 256.0)));
  g_pcmCount = 256;
  g_state = AudioState::PLAYING;
}
static void setNoise(double amp, unsigned seed) {
  std::srand(seed);
  for (int i = 0; i < 256; ++i) g_pcm[i] = static_cast<int16_t>(((std::rand() % 65536) - 32768) * amp);
  g_pcmCount = 256;
  g_state = AudioState::PLAYING;
}
static void setSilentStopped() {
  g_state = AudioState::STOPPED;
  g_pcmCount = 0;
}

struct Rig {
  lv_obj_t* scr;
  VisualizerApp* app;
  lv_obj_t* root;
  uint32_t now;

  Rig() : scr(nullptr), app(nullptr), root(nullptr), now(1000) {}

  void open() {
    scr = lv_obj_create(nullptr);
    app = new VisualizerApp();
    app->onCreate(scr);
    root = scr->kids.empty() ? nullptr : scr->kids[0];
  }
  // Đúng thứ tự AppManager (mục 6.3): onPause, onDestroy, dọn group, hủy screen, delete app.
  void close() {
    app->onPause();
    app->onDestroy();
    lv_group_remove_all_objs(&g_group);
    lv_obj_del(scr);
    delete app;
    app = nullptr;
    scr = nullptr;
    root = nullptr;
  }
  void ticks(int n) {
    for (int i = 0; i < n; ++i) {
      now += 40;
      app->onTick(now);
    }
  }
  // Mô phỏng indev keypad của LVGL 8.3
  void press(uint32_t key) {
    if (key == LV_KEY_NEXT) {
      lv_group_focus_next(&g_group);
    } else if (key == LV_KEY_PREV) {
      lv_group_focus_prev(&g_group);
    } else {
      lv_obj_t* f = lv_group_get_focused(&g_group);
      if (f) lv_stub_send_event(f, LV_EVENT_KEY, key);
    }
  }
  // Mọi rect đặc đang hiển thị (thanh + vạch đỉnh), bỏ root, widget gác và line
  std::vector<lv_obj_t*> rects() const {
    std::vector<lv_obj_t*> r;
    for (size_t i = 0; i < root->kids.size(); ++i) {
      lv_obj_t* o = root->kids[i];
      if (o->is_line || o->bg_opa != LV_OPA_COVER || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
      r.push_back(o);
    }
    return r;
  }
  lv_obj_t* line() const {
    for (size_t i = 0; i < root->kids.size(); ++i)
      if (root->kids[i]->is_line) return root->kids[i];
    return nullptr;
  }
  bool lineVisible() const { return !lv_obj_has_flag(line(), LV_OBJ_FLAG_HIDDEN); }
  bool allInBounds() const {
    std::vector<lv_obj_t*> r = rects();
    for (size_t i = 0; i < r.size(); ++i) {
      lv_obj_t* o = r[i];
      if (o->x < 0 || o->y < 0 || o->w <= 0 || o->h <= 0 || o->x + o->w > UI_W || o->y + o->h > UI_H) return false;
    }
    return true;
  }
};

// Chia rect thành thanh và vạch đỉnh theo độ dày (vạch đỉnh dày 2 hoặc rộng 2, thanh thì không cố định) là khó,
// nên test đếm theo tổng số và dùng thứ tự tạo (bars trước, caps sau) khi cần.

static void testCreate(Rig& r) {
  std::printf("[tao app, focus, bo cuc mac dinh]\n");
  CHECK(r.root != nullptr, "root");
  CHECK(r.root->w == UI_W && r.root->h == UI_H && r.root->x == 0 && r.root->y == 0, "root size %d x %d", r.root->w, r.root->h);
  CHECK(r.root->bg_opa == LV_OPA_COVER && r.root->bg_color.r == 0 && r.root->bg_color.g == 0 && r.root->bg_color.b == 0,
        "nen khong den");
  CHECK(lv_group_get_focused(&g_group) == r.root, "focus khong o widget chinh");
  CHECK(g_group.objs.size() == 3, "group co %zu object", g_group.objs.size());
  CHECK(r.line() != nullptr && !r.lineVisible(), "line phai an o kieu 1");
  // 24 thanh + 48 vach dinh
  CHECK(r.rects().size() == 72, "so rect %zu (mong 72)", r.rects().size());
  CHECK(r.allInBounds(), "co rect ra ngoai man hinh");
  CHECK(g_goHome == 0, "goHome bi goi luc tao");
  CHECK(g_readCalls == 0, "doc PCM luc tao");
}

static void testCenterSilence(Rig& r) {
  std::printf("[kieu 1: im lang -> thanh thap nhat, khong doc PCM khi khong phat]\n");
  setSilentStopped();
  const int before = g_readCalls;
  r.ticks(10);
  CHECK(g_readCalls == before, "van goi readPcmTap khi STOPPED");
  std::vector<lv_obj_t*> rs = r.rects();
  int bars = 0;
  for (size_t i = 0; i < 24; ++i) {   // 24 thanh tao truoc
    lv_obj_t* b = r.root->kids[3 + i];   // line, sentinel, sentinel, roi bars
    CHECK(b->h == 2, "thanh %zu cao %d", i, b->h);
    CHECK(b->y * 2 + b->h == UI_H, "thanh %zu khong doi xung qua tam", i);
    ++bars;
  }
  CHECK(bars == 24, "bars");
}

static void testCenterSound(Rig& r) {
  std::printf("[kieu 1: co nhac -> thanh cao len, doi xung, vach dinh, roi ve 0 khi dung]\n");
  setSine(40, 0.5);
  r.ticks(30);
  int tallest = 0;
  bool sym = true;
  for (int i = 0; i < 24; ++i) {
    lv_obj_t* b = r.root->kids[3 + i];
    if (b->h > tallest) tallest = b->h;
    if (b->y * 2 + b->h != UI_H) sym = false;
  }
  CHECK(tallest > UI_H / 4, "thanh cao nhat chi %d", tallest);
  CHECK(sym, "mat doi xung");
  CHECK(r.allInBounds(), "ra ngoai man hinh khi co nhac");
  CHECK(g_readCalls > 0, "khong doc PCM khi PLAYING");

  // Vach dinh phai nam ngoai than thanh o thanh cao nhat
  int ti = 0;
  for (int i = 0; i < 24; ++i)
    if (r.root->kids[3 + i]->h > r.root->kids[3 + ti]->h) ti = i;
  lv_obj_t* bar = r.root->kids[3 + ti];
  lv_obj_t* capTop = r.root->kids[3 + 24 + ti];
  lv_obj_t* capBot = r.root->kids[3 + 24 + 24 + ti];
  CHECK(capTop->y + capTop->h <= bar->y, "vach tren de len than thanh");
  CHECK(capBot->y >= bar->y + bar->h, "vach duoi de len than thanh");

  setSilentStopped();
  r.ticks(150);
  bool flat = true;
  for (int i = 0; i < 24; ++i)
    if (r.root->kids[3 + i]->h != 2) flat = false;
  CHECK(flat, "thanh chua ha ve 0 sau khi dung");
  bool capsHome = true;
  for (int i = 0; i < 24; ++i)
    if (r.root->kids[3 + 24 + i]->y != UI_H / 2 - 1 - 2 - 1) capsHome = false;
  CHECK(capsHome, "vach dinh chua ve vi tri thap nhat");
}

static void testWave(Rig& r) {
  std::printf("[kieu 2: waveform, phim 9 doi kieu, 4/6 chinh bien do]\n");
  r.press(LV_KEY_NEXT);
  CHECK(lv_group_get_focused(&g_group) == r.root, "focus khong tra ve widget chinh sau phim 9");
  CHECK(r.lineVisible(), "line phai hien o kieu 2");
  CHECK(r.rects().empty(), "con rect hien o kieu waveform: %zu", r.rects().size());
  lv_obj_t* ln = r.line();
  CHECK(ln->pts_n == 80, "so diem %d", ln->pts_n);
  CHECK(ln->w == UI_W && ln->y >= 0 && ln->y + ln->h <= UI_H, "line ra ngoai man hinh");

  setSilentStopped();
  r.ticks(3);
  bool flat = true;
  for (int i = 1; i < ln->pts_n; ++i)
    if (ln->pts[i].y != ln->pts[0].y) flat = false;
  CHECK(flat, "khong co nhac nhung song khong phang");

  auto spread = [&](double amp) {
    setSine(3, amp);
    r.ticks(2);
    int lo = 9999, hi = -9999;
    for (int i = 0; i < ln->pts_n; ++i) {
      if (ln->pts[i].y < lo) lo = ln->pts[i].y;
      if (ln->pts[i].y > hi) hi = ln->pts[i].y;
      CHECK(ln->pts[i].y >= 0 && ln->pts[i].y <= ln->h, "diem %d ra ngoai line: y=%d h=%d", i, ln->pts[i].y, ln->h);
      CHECK(ln->pts[i].x >= 0 && ln->pts[i].x < UI_W, "diem %d x=%d", i, ln->pts[i].x);
    }
    return hi - lo;
  };
  const int s1 = spread(0.1);   // gain x1 (mac dinh)
  r.press(LV_KEY_RIGHT);        // x2
  const int s2 = spread(0.1);
  r.press(LV_KEY_RIGHT);        // x4
  const int s3 = spread(0.1);
  r.press(LV_KEY_RIGHT);        // da toi da, khong doi
  const int s4 = spread(0.1);
  CHECK(s1 > 4, "song qua nho: %d", s1);
  CHECK(s2 > s1 && s3 > s2, "bien do khong tang: %d %d %d", s1, s2, s3);
  CHECK(s4 == s3, "vuot gioi han gain: %d vs %d", s4, s3);
  r.press(LV_KEY_LEFT);
  r.press(LV_KEY_LEFT);
  r.press(LV_KEY_LEFT);
  r.press(LV_KEY_LEFT);
  r.press(LV_KEY_LEFT);   // ve x0.5 va giu nguyen
  const int s5 = spread(0.1);
  CHECK(s5 < s1, "x0.5 khong nho hon x1: %d vs %d", s5, s1);

  // Toan thang: phai bi kep, khong tran
  r.press(LV_KEY_RIGHT);
  r.press(LV_KEY_RIGHT);
  r.press(LV_KEY_RIGHT);
  setSine(5, 1.0);
  r.ticks(2);
  for (int i = 0; i < ln->pts_n; ++i)
    CHECK(ln->pts[i].y >= 0 && ln->pts[i].y <= ln->h, "toan thang x4: diem %d y=%d", i, ln->pts[i].y);
  r.press(LV_KEY_LEFT);
  r.press(LV_KEY_LEFT);   // gain x1 cho cac buoc sau (index 1)
}

static void testEdges(Rig& r) {
  std::printf("[kieu 3: hai vien, doi xung trai/phai]\n");
  r.press(LV_KEY_NEXT);   // wave -> edges
  CHECK(!r.lineVisible(), "line con hien o kieu 3");
  CHECK(lv_group_get_focused(&g_group) == r.root, "focus");
  CHECK(r.rects().size() == 96, "so rect %zu (mong 96: 48 thanh + 48 vach)", r.rects().size());
  setSine(30, 0.6);
  r.ticks(30);
  CHECK(r.allInBounds(), "ra ngoai man hinh");
  int maxW = 0;
  for (int i = 0; i < 24; ++i) {   // 24 thanh dau tien trong pool la nua trai (bars_[0..23])
    lv_obj_t* L = r.root->kids[3 + i];
    CHECK(L->x == 0, "thanh trai %d x=%d", i, L->x);
    if (L->w > maxW) maxW = L->w;
  }
  // Doi xung hinh hoc: moi rect (thanh hoac vach dinh) deu co mot rect guong cung y, h, w qua truc giua man hinh.
  // Khong dua vao thu tu tao object (pool lon dan khi doi kieu).
  std::vector<lv_obj_t*> rs = r.rects();
  bool mirror = true;
  for (size_t a = 0; a < rs.size(); ++a) {
    bool found = false;
    for (size_t b = 0; b < rs.size(); ++b)
      if (b != a && rs[b]->y == rs[a]->y && rs[b]->h == rs[a]->h && rs[b]->w == rs[a]->w &&
          rs[b]->x == UI_W - rs[a]->x - rs[a]->w)
        found = true;
    if (!found) mirror = false;
  }
  CHECK(mirror, "trai/phai khong doi xung");
  int rightBars = 0;
  for (size_t a = 0; a < rs.size(); ++a)
    if (rs[a]->x + rs[a]->w == UI_W) ++rightBars;
  CHECK(rightBars == 24, "so thanh sat vien phai: %d (mong 24)", rightBars);
  CHECK(maxW > UI_W / 8, "thanh cao nhat chi dai %d", maxW);
  CHECK(maxW <= UI_W * 3 / 10, "thanh dai %d vuot gioi han", maxW);
  // Cac dai xep doc khong de len nhau
  for (int i = 0; i < 23; ++i) {
    lv_obj_t* lo = r.root->kids[3 + i];       // dai thap hon
    lv_obj_t* hi = r.root->kids[3 + i + 1];   // dai cao hon, nam phia tren
    CHECK(hi->y + hi->h <= lo->y, "dai %d de len dai %d", i + 1, i);
  }
  setSilentStopped();
  r.ticks(150);
  bool minimal = true;
  for (int i = 0; i < 24; ++i)
    if (r.root->kids[3 + i]->w != 2) minimal = false;
  CHECK(minimal, "thanh chua ha ve 0");
}

static void testStyleCycle(Rig& r) {
  std::printf("[doi kieu bang 7/9, quay vong]\n");
  // dang o kieu 3 (edges). 9 -> kieu 1, 9 -> kieu 2, 7 -> kieu 1, 7 -> kieu 3
  r.press(LV_KEY_NEXT);
  CHECK(!r.lineVisible() && r.rects().size() == 72, "9: edges -> center");
  r.press(LV_KEY_NEXT);
  CHECK(r.lineVisible(), "9: center -> wave");
  r.press(LV_KEY_PREV);
  CHECK(!r.lineVisible() && r.rects().size() == 72, "7: wave -> center");
  r.press(LV_KEY_PREV);
  CHECK(!r.lineVisible() && r.rects().size() == 96, "7: center -> edges (quay vong)");
  CHECK(lv_group_get_focused(&g_group) == r.root, "focus sau chuoi phim");
  r.press(LV_KEY_NEXT);   // ve kieu 1 cho cac test sau
}

static void testDensity(Rig& r) {
  std::printf("[phim 4/6: so thanh 16/24/32/48, kep o hai dau]\n");
  const int want[] = {32, 48, 48};
  for (int k = 0; k < 3; ++k) {
    r.press(LV_KEY_RIGHT);
    CHECK(r.rects().size() == static_cast<size_t>(want[k] * 3), "RIGHT %d: %zu rect (mong %d)", k, r.rects().size(), want[k] * 3);
    setNoise(0.5, 100 + k);
    r.ticks(5);
    CHECK(r.allInBounds(), "ra ngoai man hinh voi %d thanh", want[k]);
  }
  const int want2[] = {32, 24, 16, 16};
  for (int k = 0; k < 4; ++k) {
    r.press(LV_KEY_LEFT);
    CHECK(r.rects().size() == static_cast<size_t>(want2[k] * 3), "LEFT %d: %zu rect (mong %d)", k, r.rects().size(), want2[k] * 3);
    setNoise(0.5, 200 + k);
    r.ticks(5);
    CHECK(r.allInBounds(), "ra ngoai man hinh voi %d thanh", want2[k]);
  }
  r.press(LV_KEY_RIGHT);   // ve 24
  CHECK(r.rects().size() == 72, "ve 24 thanh");
  // Kieu 3 cung dung chung so thanh, kiem tra gioi han 48 thanh nam tron trong man hinh
  r.press(LV_KEY_NEXT);
  r.press(LV_KEY_NEXT);   // edges
  r.press(LV_KEY_RIGHT);
  r.press(LV_KEY_RIGHT);  // 48
  setNoise(0.9, 7);
  r.ticks(20);
  CHECK(r.rects().size() == 48 * 4, "edges 48 thanh: %zu rect", r.rects().size());
  CHECK(r.allInBounds(), "edges 48 thanh ra ngoai man hinh");
  r.press(LV_KEY_LEFT);
  r.press(LV_KEY_LEFT);   // 24
  r.press(LV_KEY_NEXT);   // ve kieu 1 (center)
  CHECK(r.rects().size() == 72, "ve center 24");
}

static void testEsc(Rig& r) {
  std::printf("[ESC, phim khong dung]\n");
  const int g0 = g_goHome;
  r.press(LV_KEY_ESC);
  CHECK(g_goHome == g0 + 1, "ESC khong goi goHome (%d -> %d)", g0, g_goHome);
  const size_t rects = r.rects().size();
  r.press(LV_KEY_UP);
  r.press(LV_KEY_DOWN);
  r.press(LV_KEY_ENTER);
  CHECK(g_goHome == g0 + 1, "UP/DOWN/ENTER lam goHome");
  CHECK(r.rects().size() == rects, "UP/DOWN/ENTER lam doi giao dien");
}

static void testFrameLimit(Rig& r) {
  std::printf("[gioi han toc do khung]\n");
  setSine(20, 0.5);
  r.now += 1000;
  r.app->onTick(r.now);
  const int c0 = g_readCalls;
  for (int i = 1; i <= 5; ++i) r.app->onTick(r.now + i * 5);   // 25 ms, chua du 40 ms
  CHECK(g_readCalls == c0, "tick qua nhanh van xu ly (%d -> %d)", c0, g_readCalls);
  r.app->onTick(r.now + 40);
  CHECK(g_readCalls == c0 + 1, "tick du 40 ms khong xu ly");
  // Cuon so millis() (uint32) khong lam ket khung
  r.now = 0xFFFFFFF0u;
  r.app->onTick(r.now);
  const int c1 = g_readCalls;
  r.app->onTick(r.now + 50);   // vuot qua 0
  CHECK(g_readCalls == c1 + 1, "cuon millis() lam ket khung");
}

static void testPartialPcm(Rig& r) {
  std::printf("[PCM thieu mau, PCM nhieu hon yeu cau]\n");
  setSine(30, 0.5);
  g_pcmCount = 100;   // chi co 100 mau
  r.ticks(5);
  CHECK(r.allInBounds(), "100 mau");
  g_pcmCount = 1;
  r.ticks(5);
  CHECK(r.allInBounds(), "1 mau");
  g_pcmCount = 256;
  r.ticks(2);
  r.press(LV_KEY_NEXT);   // wave
  g_pcmCount = 7;
  r.ticks(3);
  lv_obj_t* ln = r.line();
  for (int i = 0; i < ln->pts_n; ++i) CHECK(ln->pts[i].y >= 0 && ln->pts[i].y <= ln->h, "wave 7 mau: diem %d", i);
  r.press(LV_KEY_PREV);   // ve center
  g_pcmCount = 256;
}

static void testThemeColors() {
  std::printf("[mau theme: toi qua thi roi ve mau sang hon]\n");
  Theme saved = g_theme;
  // accent rat toi, focus trung binh, text sang -> thanh phai dung focus hoac text, vach dung focus
  g_theme.accent = 0x101010;
  g_theme.focus = 0x202020;
  g_theme.text = 0xE0E0E0;
  Rig r;
  r.open();
  lv_obj_t* bar = r.root->kids[3];
  lv_obj_t* cap = r.root->kids[3 + 24];
  CHECK(bar->bg_color.r == 0xE0 && cap->bg_color.r == 0xE0, "thanh/vach khong roi ve text: %02X %02X", bar->bg_color.r, cap->bg_color.r);
  r.close();
  // Ca ba deu toi -> trang
  g_theme.text = 0x080808;
  r.open();
  CHECK(r.root->kids[3]->bg_color.r == 255, "ca ba toi nhung khong ve trang");
  r.close();
  // Theme sang -> dung accent cho thanh, focus cho vach
  g_theme = saved;
  r.open();
  CHECK(r.root->kids[3]->bg_color.r == 0xE0 && r.root->kids[3]->bg_color.g == 0x40, "thanh khong dung accent");
  CHECK(r.root->kids[3 + 24]->bg_color.r == 0xFF, "vach khong dung focus");
  r.close();
}

static void testDestroyedCallbacks() {
  std::printf("[callback chay sau onDestroy() khong lam gi, khong crash]\n");
  Rig r;
  r.open();
  lv_obj_t* root = r.root;
  lv_obj_t* prev = g_group.objs[0];
  lv_obj_t* next = g_group.objs[2];
  const int g0 = g_goHome;
  r.app->onPause();
  r.app->onDestroy();
  // LVGL con gui su kien truoc khi screen bi huy
  lv_stub_send_event(root, LV_EVENT_KEY, LV_KEY_ESC);
  lv_stub_send_event(root, LV_EVENT_KEY, LV_KEY_RIGHT);
  lv_stub_send_event(next, LV_EVENT_FOCUSED, 0);
  lv_stub_send_event(prev, LV_EVENT_FOCUSED, 0);
  lv_stub_send_event(root, LV_EVENT_FOCUSED, 0);
  CHECK(g_goHome == g0, "ESC sau onDestroy van goi goHome");
  r.app->onTick(99999);   // tick sau onDestroy
  lv_group_remove_all_objs(&g_group);
  lv_stub_send_event(root, LV_EVENT_KEY, LV_KEY_ESC);
  lv_obj_del(r.scr);
  delete r.app;
  CHECK(g_goHome == g0, "goHome khi dong app");
  CHECK(g_stub_live_objs == 0, "con %d object", g_stub_live_objs);
}

static void testPersistence() {
  std::printf("[kieu va muc duoc nho qua cac lan vao app]\n");
  Rig r;
  r.open();
  r.press(LV_KEY_NEXT);   // wave
  r.press(LV_KEY_RIGHT);  // gain x2
  r.close();
  r.open();
  CHECK(r.lineVisible(), "kieu khong duoc nho");
  r.close();
  r.open();
  r.press(LV_KEY_PREV);   // center
  CHECK(r.rects().size() == 72, "kieu 1 sau khi vao lai");
  r.close();
  r.open();
  CHECK(r.rects().size() == 72, "kieu 1 khong duoc nho");
  r.close();
}

static void testLeaks() {
  std::printf("[300 lan vao/ra app, moi lan doi kieu, khong ro ri object]\n");
  const int base = g_stub_live_objs;
  Rig r;
  for (int i = 0; i < 300; ++i) {
    r.open();
    setNoise(0.4, 1000 + i);
    r.ticks(3);
    r.press(i % 2 ? LV_KEY_NEXT : LV_KEY_PREV);
    r.press(LV_KEY_RIGHT);
    r.ticks(3);
    r.press(LV_KEY_NEXT);
    r.press(LV_KEY_LEFT);
    r.ticks(2);
    r.close();
    if (g_stub_live_objs != base) {
      CHECK(false, "vong %d: %d object (mong %d)", i, g_stub_live_objs, base);
      break;
    }
    if (!g_group.objs.empty() || g_group.focus != -1) {
      CHECK(false, "vong %d: group chua sach", i);
      break;
    }
  }
  CHECK(g_stub_live_objs == base, "tong ket %d object (mong %d)", g_stub_live_objs, base);
}

static void testFuzz() {
  std::printf("[nhieu phim ngau nhien 4000 lan: focus luon o widget chinh, khong ra ngoai man hinh]\n");
  const uint32_t keys[] = {LV_KEY_NEXT, LV_KEY_PREV, LV_KEY_LEFT, LV_KEY_RIGHT, LV_KEY_UP, LV_KEY_DOWN, LV_KEY_ENTER};
  Rig r;
  r.open();
  std::srand(2026);
  bool focusOk = true, boundsOk = true, lineOk = true;
  const int g0 = g_goHome;
  for (int i = 0; i < 4000; ++i) {
    r.press(keys[std::rand() % 7]);
    if (std::rand() % 3 == 0) {
      if (std::rand() % 5 == 0) setSilentStopped(); else setNoise((std::rand() % 100) / 100.0, std::rand());
      r.ticks(1 + std::rand() % 3);
    }
    if (lv_group_get_focused(&g_group) != r.root) focusOk = false;
    if (!r.allInBounds()) boundsOk = false;
    lv_obj_t* ln = r.line();
    if (r.lineVisible()) {
      for (int p = 0; p < ln->pts_n; ++p)
        if (ln->pts[p].y < 0 || ln->pts[p].y > ln->h) lineOk = false;
    }
  }
  CHECK(focusOk, "focus roi khoi widget chinh");
  CHECK(boundsOk, "co rect ra ngoai man hinh");
  CHECK(lineOk, "co diem song ra ngoai line");
  CHECK(g_goHome == g0, "goHome bi goi ngoai y muon");
  r.close();
  CHECK(g_stub_live_objs == 0, "con %d object", g_stub_live_objs);
}

int main() {
  std::printf("man hinh %d x %d%s\n", UI_W, UI_H, UI_COMPACT ? " (compact)" : "");
  {
    Rig r;
    r.open();
    testCreate(r);
    testCenterSilence(r);
    testCenterSound(r);
    testWave(r);
    testEdges(r);
    testStyleCycle(r);
    testDensity(r);
    testEsc(r);
    testFrameLimit(r);
    testPartialPcm(r);
    r.close();
    CHECK(g_stub_live_objs == 0, "con %d object sau khi dong", g_stub_live_objs);
  }
  testThemeColors();
  testDestroyedCallbacks();
  testPersistence();
  testLeaks();
  testFuzz();
  std::printf("\n%d kiem tra, %d loi\n", gChecks, gFails);
  return gFails == 0 ? 0 : 1;
}
