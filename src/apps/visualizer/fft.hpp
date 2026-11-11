#pragma once
// FFT thuần cho Visualizer: không include Arduino/LVGL, test được trên máy tính.
//
// Không thread-safe: dùng một bộ đệm làm việc tĩnh, chỉ gọi từ một task
// (Core 1, trong VisualizerApp::onTick). Bảng và bộ đệm tổng cộng khoảng 5 KB
// nên để trong RAM trong (truy cập nhanh hơn PSRAM), không cần cấp bằng Psram.

#include <cstddef>
#include <cstdint>

namespace Fft {

// Đổi kích thước: sửa một dòng này (phải là lũy thừa của 2, tối đa 1024).
constexpr size_t kSize = 256;
constexpr size_t kBins = kSize / 2;   // bin 0..kBins-1 (bin 0 là thành phần DC)
constexpr int kMaxBands = 64;

// Dựng bảng (cửa sổ Hann, twiddle, đảo bit). Gọi lại an toàn.
// magnitude() tự gọi init() nếu chưa gọi.
void init();

// Tính phổ biên độ từ kSize mẫu PCM 16-bit mono.
// outMag có kBins phần tử. Chuẩn hóa: sin đủ biên độ (±32767) đúng tâm một bin cho 1.0.
// Im lặng cho 0.0. Giá trị có thể hơi vượt 1.0 do làm tròn, người gọi tự kẹp.
void magnitude(const int16_t* samples, float* outMag);

// Gộp phổ thành `bands` dải theo thang log (thấp mịn, cao thô), mỗi dải lấy đỉnh các bin của nó.
// Bỏ bin 0 (DC). Mỗi dải có ít nhất một bin, các dải liền nhau và phủ hết bin 1..kBins-1.
// bands được kẹp vào [0, kMaxBands]; bands = 0 thì không ghi gì. out có `bands` phần tử.
void bandLevels(const float* mag, int bands, float* out);

}  // namespace Fft