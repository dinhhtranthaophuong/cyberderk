#include "apps/visualizer/visualizer_app.hpp"

#include <cmath>
#include <cstring>

#include "config/display.hpp"
#include "core/app_factories.hpp"
#include "core/app_manager.hpp"
#include "core/theme.hpp"
#include "services/audio_service.hpp"
#include "services/input_service.hpp"

namespace {

// ---- Các hằng số chỉnh được (đổi ở đây, không sửa logic) -------------------------------------
const int kBandOptions[] = {16, 24, 32, 48};    // phím 4/6 ở kiểu 1 và 3
const float kGains[] = {0.5f, 1.0f, 2.0f, 4.0f};  // phím 4/6 ở waveform
const int kOptionCount = 4;

const uint32_t kFrameMs = 40;        // ~25 fps. Hạ xuống 33 nếu board còn dư sức, tăng lên nếu giật
const float kDbFloor = -80.0f;       // dB ứng với thanh thấp nhất (sin toàn thang = 0 dB)
const float kDbRange = 65.0f;        // khoảng dB từ đáy tới thanh cao nhất
const float kTiltDb = 15.0f;         // cộng thêm dần tới dải cao nhất vì nhạc ít năng lượng ở treble
const float kAttack = 0.65f;         // lên nhanh
const float kFall = 0.16f;           // xuống chậm
const int kPeakHoldFrames = 14;      // giữ vạch đỉnh ~0,5 giây
const float kPeakFall = 0.025f;
const float kSnapZero = 0.004f;
const int kMinLuma = 80;             // màu theme tối hơn mức này thì khó thấy trên nền đen

const int kMargin = UI_COMPACT ? 4 : 8;
const int kVMargin = UI_COMPACT ? 6 : 12;

// ---- Lựa chọn của người dùng, nhớ qua các lần vào/ra app --------------------------------------
int sStyle = 0;
int sBandIdx = 1;   // 24 thanh
int sGainIdx = 1;   // x1

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int luma(uint32_t c) {
  return static_cast<int>(((((c >> 16) & 0xFF) * 299u) + (((c >> 8) & 0xFF) * 587u) + ((c & 0xFF) * 114u)) / 1000u);
}

// Màu từ theme, lấy ứng viên đầu tiên đủ sáng trên nền đen; không có thì trắng.
lv_color_t pickBright(uint32_t a, uint32_t b, uint32_t c) {
  if (luma(a) >= kMinLuma) return lv_color_hex(a);
  if (luma(b) >= kMinLuma) return lv_color_hex(b);
  if (luma(c) >= kMinLuma) return lv_color_hex(c);
  return lv_color_white();
}

// level 0..1 -> chiều dài 2..full (chẵn, để thanh đối xứng qua tâm không lệch nửa pixel)
int lenFor(float level, int full) {
  int len = 2 + static_cast<int>(level * static_cast<float>(full - 2) + 0.5f);
  if (len > full) len = full;
  return len & ~1;
}

}  // namespace

// ================================================================================================
// Vòng đời
// ================================================================================================

void VisualizerApp::onCreate(lv_obj_t* parent) {
  alive_ = true;
  ready_ = false;
  style_ = clampInt(sStyle, 0, STYLE_COUNT - 1);
  haveFrame_ = false;
  waveFlat_ = false;
  waveDrawn_ = false;
  barsMade_ = 0;
  capsMade_ = 0;
  for (int i = 0; i < kPoolMax; ++i) {
    bars_[i] = nullptr;
    caps_[i] = nullptr;
  }

  // Màu từ theme. Nền đen cố định là ngoại lệ đã ghi trong ARCHITECTURE.md mục 7.1.
  const Theme& th = Themes::current();
  barColor_ = pickBright(th.accent, th.focus, th.text);
  capColor_ = pickBright(th.focus, th.text, th.accent);

  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_set_pos(root_, 0, 0);
  lv_obj_set_size(root_, UI_W, UI_H);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root_, lv_color_black(), 0);

  line_ = lv_line_create(root_);
  lv_obj_remove_style_all(line_);
  lv_obj_set_style_line_width(line_, 2, 0);
  lv_obj_set_style_line_color(line_, barColor_, 0);
  lv_obj_set_style_line_rounded(line_, false, 0);
  lv_obj_add_flag(line_, LV_OBJ_FLAG_HIDDEN);

  // Hai widget gác cho phím 7/9: 1x1, trong suốt, không có style nên không vẽ gì cả.
  lv_obj_t** sentinels[2] = {&sentinelPrev_, &sentinelNext_};
  for (int i = 0; i < 2; ++i) {
    lv_obj_t* s = lv_obj_create(root_);
    lv_obj_remove_style_all(s);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 1, 1);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    *sentinels[i] = s;
  }

  lv_obj_t* all[3] = {sentinelPrev_, root_, sentinelNext_};   // thứ tự trong group: prev, chính, next
  for (int i = 0; i < 3; ++i) {
    lv_obj_add_event_cb(all[i], eventCb, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(all[i], eventCb, LV_EVENT_FOCUSED, this);
  }
  lv_group_t* g = InputService::defaultGroup();
  if (g != nullptr) {
    for (int i = 0; i < 3; ++i) lv_group_add_obj(g, all[i]);
    lv_group_focus_obj(root_);
  }

  applyLayout();
  ready_ = true;
}

void VisualizerApp::onDestroy() {
  // Callback có thể còn chạy tới khi app bị delete: chặn bằng alive_/ready_, và bỏ mọi con trỏ widget.
  alive_ = false;
  ready_ = false;
  root_ = nullptr;
  line_ = nullptr;
  sentinelPrev_ = nullptr;
  sentinelNext_ = nullptr;
  for (int i = 0; i < kPoolMax; ++i) {
    bars_[i] = nullptr;
    caps_[i] = nullptr;
  }
  barsMade_ = 0;
  capsMade_ = 0;
}

// ================================================================================================
// Phím
// ================================================================================================

void VisualizerApp::eventCb(lv_event_t* e) {
  VisualizerApp* self = static_cast<VisualizerApp*>(lv_event_get_user_data(e));
  if (self == nullptr || !self->alive_) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_KEY) {
    self->handleKey(lv_event_get_key(e));
  } else if (code == LV_EVENT_FOCUSED) {
    self->handleFocused(lv_event_get_target(e));
  }
}

void VisualizerApp::handleKey(uint32_t key) {
  if (!ready_) return;
  switch (key) {
    case LV_KEY_ESC:
      AppManager::goHome();
      break;
    case LV_KEY_LEFT:
      changeLevel(-1);
      break;
    case LV_KEY_RIGHT:
      changeLevel(+1);
      break;
    default:
      break;
  }
}

void VisualizerApp::handleFocused(lv_obj_t* target) {
  if (!ready_ || root_ == nullptr) return;
  int delta = 0;
  if (target == sentinelNext_) {
    delta = +1;
  } else if (target == sentinelPrev_) {
    delta = -1;
  } else {
    return;
  }
  lv_group_focus_obj(root_);   // trả focus ngay để lần bấm sau vẫn rơi vào widget gác kế bên
  changeStyle(delta);
}

void VisualizerApp::changeStyle(int delta) {
  style_ = (style_ + delta + STYLE_COUNT) % STYLE_COUNT;
  sStyle = style_;
  applyLayout();
}

void VisualizerApp::changeLevel(int delta) {
  int& idx = (style_ == STYLE_WAVE) ? sGainIdx : sBandIdx;
  const int next = clampInt(idx + delta, 0, kOptionCount - 1);
  if (next == idx) return;
  idx = next;
  applyLayout();
}

// ================================================================================================
// Dựng object và bố cục. Chỉ chạy khi đổi kiểu/mức (không chạy mỗi khung).
// ================================================================================================

lv_obj_t* VisualizerApp::makeRect(lv_color_t color) {
  lv_obj_t* o = lv_obj_create(root_);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

void VisualizerApp::ensurePool(int bars, int caps) {
  if (bars > kPoolMax) bars = kPoolMax;
  if (caps > kPoolMax) caps = kPoolMax;
  while (barsMade_ < bars) {
    bars_[barsMade_] = makeRect(barColor_);
    ++barsMade_;
  }
  while (capsMade_ < caps) {
    caps_[capsMade_] = makeRect(capColor_);
    ++capsMade_;
  }
}

void VisualizerApp::applyLayout() {
  if (root_ == nullptr) return;

  static_assert(sizeof(kBandOptions) / sizeof(kBandOptions[0]) == 4, "kOptionCount");

  for (int i = 0; i < barsMade_; ++i) lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < capsMade_; ++i) lv_obj_add_flag(caps_[i], LV_OBJ_FLAG_HIDDEN);

  bands_ = kBandOptions[clampInt(sBandIdx, 0, kOptionCount - 1)];
  if (bands_ > kMaxBands) bands_ = kMaxBands;
  for (int i = 0; i < kMaxBands; ++i) {
    smooth_[i] = 0.0f;
    peak_[i] = 0.0f;
    hold_[i] = 0;
    raw_[i] = 0.0f;
    lastLen_[i] = -1;
    lastPeak_[i] = -1;
  }
  cy_ = UI_H / 2;
  capT_ = 2;

  if (style_ == STYLE_WAVE) {
    const int half = UI_H * 3 / 10;
    waveTop_ = cy_ - half - 4;
    waveH_ = 2 * half + 8;
    lv_obj_set_pos(line_, 0, waveTop_);
    lv_obj_set_size(line_, UI_W, waveH_);
    for (int i = 0; i < kWavePts; ++i) {
      wavePts_[i].x = static_cast<lv_coord_t>(kMargin / 2 + i * (UI_W - kMargin) / (kWavePts - 1));
      wavePts_[i].y = static_cast<lv_coord_t>(waveH_ / 2);
    }
    lv_obj_clear_flag(line_, LV_OBJ_FLAG_HIDDEN);
    lv_line_set_points(line_, wavePts_, kWavePts);
    waveDrawn_ = true;
    waveFlat_ = true;
    return;
  }

  lv_obj_add_flag(line_, LV_OBJ_FLAG_HIDDEN);

  if (style_ == STYLE_CENTER) {
    const int slot = (UI_W - 2 * kMargin) / bands_ < 2 ? 2 : (UI_W - 2 * kMargin) / bands_;
    const int gap = slot / 4 < 1 ? 1 : slot / 4;
    const int thick = slot - gap;
    const int x0 = (UI_W - slot * bands_) / 2 + gap / 2;
    fullLen_ = 2 * (UI_H / 2 - kVMargin);
    ensurePool(bands_, 2 * bands_);
    for (int i = 0; i < bands_; ++i) {
      const int x = x0 + i * slot;
      lv_obj_set_x(bars_[i], x);
      lv_obj_set_width(bars_[i], thick);
      lv_obj_clear_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
      for (int k = 0; k < 2; ++k) {   // k = 0: vạch đỉnh phía trên, k = 1: phía dưới
        lv_obj_t* c = caps_[k * bands_ + i];
        lv_obj_set_x(c, x);
        lv_obj_set_width(c, thick);
        lv_obj_set_height(c, capT_);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
      }
    }
    renderCenter();
  } else {  // STYLE_EDGES
    const int avail = UI_H - 2 * kVMargin;
    const int slot = avail / bands_ < 2 ? 2 : avail / bands_;
    const int gap = slot / 4 < 1 ? 1 : slot / 4;
    const int thick = slot - gap;
    const int y0 = (UI_H - slot * bands_) / 2;
    maxLen_ = (UI_W * 3 / 10) & ~1;
    ensurePool(2 * bands_, 2 * bands_);
    for (int i = 0; i < bands_; ++i) {
      // Dải 0 (trầm) ở dưới cùng, dải cao dần lên trên
      const int y = y0 + (bands_ - 1 - i) * slot + gap / 2;
      for (int k = 0; k < 2; ++k) {   // k = 0: trái, k = 1: phải
        lv_obj_t* b = bars_[k * bands_ + i];
        lv_obj_set_y(b, y);
        lv_obj_set_height(b, thick);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* c = caps_[k * bands_ + i];
        lv_obj_set_y(c, y);
        lv_obj_set_height(c, thick);
        lv_obj_set_width(c, capT_);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
      }
    }
    renderEdges();
  }
}

// ================================================================================================
// Mỗi khung hình
// ================================================================================================

void VisualizerApp::onTick(uint32_t nowMs) {
  if (!alive_ || root_ == nullptr) return;
  if (haveFrame_ && (nowMs - lastFrameMs_) < kFrameMs) return;
  haveFrame_ = true;
  lastFrameMs_ = nowMs;

  size_t n = 0;
  // Hỏi state() trước: chưa begin() hoặc đang dừng thì khỏi đụng ring buffer của audio.
  if (AudioService::state() == AudioState::PLAYING) {
    n = AudioService::readPcmTap(pcm_, Fft::kSize);
    if (n > Fft::kSize) n = Fft::kSize;
    for (size_t i = n; i < Fft::kSize; ++i) pcm_[i] = 0;
  }

  if (style_ == STYLE_WAVE) {
    updateWave(n);
  } else {
    updateSpectrum(n);
    if (style_ == STYLE_CENTER) {
      renderCenter();
    } else {
      renderEdges();
    }
  }
}

void VisualizerApp::updateSpectrum(size_t n) {
  float target[kMaxBands];
  for (int i = 0; i < kMaxBands; ++i) target[i] = 0.0f;

  if (n > 0) {
    Fft::magnitude(pcm_, mag_);
    Fft::bandLevels(mag_, bands_, raw_);
    for (int i = 0; i < bands_; ++i) {
      const float pos = bands_ > 1 ? static_cast<float>(i) / static_cast<float>(bands_ - 1) : 0.0f;
      const float db = 20.0f * log10f(raw_[i] + 1e-5f) + kTiltDb * pos;
      target[i] = clampF((db - kDbFloor) / kDbRange, 0.0f, 1.0f);
    }
  }

  for (int i = 0; i < bands_; ++i) {
    const float t = target[i];
    smooth_[i] += (t - smooth_[i]) * (t > smooth_[i] ? kAttack : kFall);
    if (t < kSnapZero && smooth_[i] < kSnapZero) smooth_[i] = 0.0f;

    if (smooth_[i] >= peak_[i]) {
      peak_[i] = smooth_[i];
      hold_[i] = static_cast<uint8_t>(kPeakHoldFrames);
    } else if (hold_[i] > 0) {
      --hold_[i];
    } else {
      peak_[i] -= kPeakFall;
      if (peak_[i] < smooth_[i]) peak_[i] = smooth_[i];
    }
  }
}

void VisualizerApp::renderCenter() {
  for (int i = 0; i < bands_; ++i) {
    const int h = lenFor(smooth_[i], fullLen_);
    if (h != lastLen_[i]) {
      lv_obj_set_height(bars_[i], h);
      lv_obj_set_y(bars_[i], cy_ - h / 2);
      lastLen_[i] = static_cast<int16_t>(h);
    }
    const int ph = lenFor(peak_[i], fullLen_);
    if (ph != lastPeak_[i]) {
      lv_obj_set_y(caps_[i], cy_ - ph / 2 - 1 - capT_);          // phía trên
      lv_obj_set_y(caps_[bands_ + i], cy_ + ph / 2 + 1);          // phía dưới
      lastPeak_[i] = static_cast<int16_t>(ph);
    }
  }
}

void VisualizerApp::renderEdges() {
  for (int i = 0; i < bands_; ++i) {
    const int len = lenFor(smooth_[i], maxLen_);
    if (len != lastLen_[i]) {
      lv_obj_t* left = bars_[i];
      lv_obj_t* right = bars_[bands_ + i];
      lv_obj_set_x(left, 0);
      lv_obj_set_width(left, len);
      lv_obj_set_x(right, UI_W - len);
      lv_obj_set_width(right, len);
      lastLen_[i] = static_cast<int16_t>(len);
    }
    const int pl = lenFor(peak_[i], maxLen_);
    if (pl != lastPeak_[i]) {
      lv_obj_set_x(caps_[i], pl + 1);
      lv_obj_set_x(caps_[bands_ + i], UI_W - pl - 1 - capT_);
      lastPeak_[i] = static_cast<int16_t>(pl);
    }
  }
}

void VisualizerApp::updateWave(size_t n) {
  const int mid = waveH_ / 2;
  if (n == 0) {
    if (waveFlat_) return;   // đã vẽ đường phẳng rồi
    for (int i = 0; i < kWavePts; ++i) wavePts_[i].y = static_cast<lv_coord_t>(mid);
    waveFlat_ = true;
  } else {
    // Canh pha: bắt đầu ở điểm cắt 0 đi lên đầu tiên của nửa đầu bộ đệm để sóng đứng yên.
    const size_t half = Fft::kSize / 2;
    size_t start = 0;
    for (size_t i = 1; i < half; ++i) {
      if (pcm_[i - 1] < 0 && pcm_[i] >= 0) {
        start = i;
        break;
      }
    }
    const float gain = kGains[clampInt(sGainIdx, 0, kOptionCount - 1)];
    const float amp = static_cast<float>(mid - 4);
    for (int p = 0; p < kWavePts; ++p) {
      const size_t from = start + static_cast<size_t>(p) * half / kWavePts;
      size_t to = start + static_cast<size_t>(p + 1) * half / kWavePts;
      if (to <= from) to = from + 1;
      int32_t sum = 0;
      for (size_t k = from; k < to; ++k) sum += pcm_[k];
      const float avg = static_cast<float>(sum) / static_cast<float>(to - from) / 32768.0f;
      const int y = mid - static_cast<int>(clampF(avg * gain, -1.0f, 1.0f) * amp);
      wavePts_[p].y = static_cast<lv_coord_t>(y);
    }
    waveFlat_ = false;
  }
  lv_line_set_points(line_, wavePts_, kWavePts);
  waveDrawn_ = true;
}

// Factory (mục 6.9): phạm vi toàn cục, không nằm trong namespace.
IApp* createVisualizerApp() { return new VisualizerApp(); }
