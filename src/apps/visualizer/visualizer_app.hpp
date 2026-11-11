#pragma once
// App Visualizer (APP_VISUALIZER): hiển thị phổ/dạng sóng của nhạc đang phát.
//
// Nguồn dữ liệu: AudioService::readPcmTap() (PCM mono 16-bit trước khi áp âm lượng).
// Không đụng SD/I2S. Thoát app thì app bị hủy, nhạc vẫn phát nền (AudioService độc lập).
//
// Phím:  7/9 đổi kiểu (trước/sau, quay vòng)   4/6 chỉnh dạng (ít/nhiều thanh; waveform: độ phóng biên độ)
//        1 (LV_KEY_ESC) về Home.  Giữ 1 = HOME do AppManager xử lý.
//
// Ghi chú phím 7/9: LVGL 8.3 không gửi LV_KEY_PREV/NEXT tới widget (indev tự dời focus), nên app
// đặt hai widget "gác" 1x1 trong suốt hai bên widget chính. Focus bị dời sang widget gác nghĩa là
// người dùng bấm 7 hoặc 9; app trả focus về widget chính ngay trong callback.
//
// Mọi hàm chạy trên Core 1 khi đang giữ LvglLock. Không có task, không có timer, không cấp PSRAM:
// toàn bộ bộ đệm của app gộp lại dưới 4 KB nên nằm trong đối tượng app.

#include <cstddef>
#include <cstdint>

#include "apps/visualizer/fft.hpp"
#include "core/app_base.hpp"

class VisualizerApp : public IApp {
 public:
  AppId id() const override { return AppId::APP_VISUALIZER; }
  void onCreate(lv_obj_t* parent) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;

 private:
  enum Style { STYLE_CENTER = 0, STYLE_WAVE = 1, STYLE_EDGES = 2, STYLE_COUNT = 3 };

  static const int kMaxBands = 48;          // số dải tối đa (lớn nhất trong kBandOptions)
  static const int kPoolMax = kMaxBands * 2;
  static const int kWavePts = 80;

  static void eventCb(lv_event_t* e);
  void handleKey(uint32_t key);
  void handleFocused(lv_obj_t* target);

  void changeStyle(int delta);
  void changeLevel(int delta);

  lv_obj_t* makeRect(lv_color_t color);
  void ensurePool(int bars, int caps);
  void applyLayout();

  void updateSpectrum(size_t n);
  void renderCenter();
  void renderEdges();
  void updateWave(size_t n);

  // Trạng thái vòng đời. Callback widget có thể chạy sau onDestroy() nên luôn kiểm tra alive_.
  bool alive_ = false;
  bool ready_ = false;   // false trong lúc dựng, để focus ban đầu không bị hiểu là phím 7/9
  int style_ = STYLE_CENTER;

  // Widget (nullptr sau onDestroy). Screen do AppManager hủy nên không del tay.
  lv_obj_t* root_ = nullptr;
  lv_obj_t* line_ = nullptr;
  lv_obj_t* sentinelPrev_ = nullptr;
  lv_obj_t* sentinelNext_ = nullptr;
  lv_obj_t* bars_[kPoolMax];
  lv_obj_t* caps_[kPoolMax];
  int barsMade_ = 0;
  int capsMade_ = 0;

  lv_color_t barColor_;
  lv_color_t capColor_;

  // Hình học của kiểu đang chọn
  int bands_ = 0;
  int cy_ = 0;
  int fullLen_ = 0;   // kiểu giữa: chiều cao tối đa của thanh (chẵn)
  int maxLen_ = 0;    // kiểu hai viền: chiều dài tối đa của thanh (chẵn)
  int capT_ = 2;      // độ dày vạch đỉnh
  int waveTop_ = 0;
  int waveH_ = 0;

  // Dữ liệu mỗi khung
  uint32_t lastFrameMs_ = 0;
  bool haveFrame_ = false;
  int16_t pcm_[Fft::kSize];
  float mag_[Fft::kBins];
  float raw_[kMaxBands];
  float smooth_[kMaxBands];
  float peak_[kMaxBands];
  uint8_t hold_[kMaxBands];
  int16_t lastLen_[kMaxBands];    // chiều dài thanh đã ghi vào LVGL (-1 = chưa ghi)
  int16_t lastPeak_[kMaxBands];
  lv_point_t wavePts_[kWavePts];
  bool waveFlat_ = false;
  bool waveDrawn_ = false;
};
