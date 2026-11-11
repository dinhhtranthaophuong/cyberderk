#include "apps/visualizer/fft.hpp"

#include <cmath>

namespace Fft {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Bảng tính sẵn (khoảng 3 KB) và bộ đệm làm việc (2 KB).
float gHann[kSize];
float gCos[kSize / 2];     // cos(2*pi*k/N)
float gSin[kSize / 2];     // sin(2*pi*k/N)
uint16_t gRev[kSize];      // đảo bit
float gRe[kSize];
float gIm[kSize];
bool gReady = false;

// Bảng ranh giới dải, lưu theo số dải gần nhất để khỏi tính lại mỗi khung.
int gEdgesBands = 0;
uint8_t gEdges[kMaxBands + 1];

void buildEdges(int bands) {
  // Dải i phủ các bin [gEdges[i], gEdges[i+1]); bin 0 (DC) bỏ qua.
  gEdges[0] = 1;
  for (int i = 1; i <= bands; ++i) {
    int ideal = static_cast<int>(
        std::lround(std::pow(static_cast<float>(kBins), static_cast<float>(i) / bands)));
    int e = ideal;
    if (e < gEdges[i - 1] + 1) e = gEdges[i - 1] + 1;           // mỗi dải ít nhất 1 bin
    int cap = static_cast<int>(kBins) - (bands - i);             // chừa chỗ cho các dải sau
    if (e > cap) e = cap;
    gEdges[i] = static_cast<uint8_t>(e);
  }
  gEdgesBands = bands;
}

}  // namespace

void init() {
  if (gReady) return;
  constexpr size_t N = kSize;
  for (size_t i = 0; i < N; ++i) {
    gHann[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(N));
  }
  for (size_t k = 0; k < N / 2; ++k) {
    float a = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(N);
    gCos[k] = std::cos(a);
    gSin[k] = std::sin(a);
  }
  int bits = 0;
  while ((static_cast<size_t>(1) << bits) < N) ++bits;
  for (size_t i = 0; i < N; ++i) {
    size_t r = 0;
    for (int b = 0; b < bits; ++b) {
      if (i & (static_cast<size_t>(1) << b)) r |= static_cast<size_t>(1) << (bits - 1 - b);
    }
    gRev[i] = static_cast<uint16_t>(r);
  }
  gReady = true;
}

void magnitude(const int16_t* samples, float* outMag) {
  init();
  constexpr size_t N = kSize;

  // Nạp theo thứ tự đảo bit, nhân cửa sổ Hann.
  for (size_t i = 0; i < N; ++i) {
    gRe[gRev[i]] = static_cast<float>(samples[i]) * gHann[i];
    gIm[gRev[i]] = 0.0f;
  }

  // Radix-2 decimation-in-time, biến đổi thuận (e^{-j...}).
  for (size_t len = 2; len <= N; len <<= 1) {
    const size_t half = len >> 1;
    const size_t step = N / len;
    for (size_t i = 0; i < N; i += len) {
      for (size_t j = 0; j < half; ++j) {
        const float wr = gCos[j * step];
        const float wi = -gSin[j * step];
        const size_t a = i + j;
        const size_t b = a + half;
        const float tr = wr * gRe[b] - wi * gIm[b];
        const float ti = wr * gIm[b] + wi * gRe[b];
        gRe[b] = gRe[a] - tr;
        gIm[b] = gIm[a] - ti;
        gRe[a] += tr;
        gIm[a] += ti;
      }
    }
  }

  // Hann có độ lợi kết hợp N/2 * 1/2, sin biên độ A cho |X| = A*N/4.
  const float scale = 4.0f / (static_cast<float>(N) * 32768.0f);
  for (size_t k = 0; k < kBins; ++k) {
    outMag[k] = std::sqrt(gRe[k] * gRe[k] + gIm[k] * gIm[k]) * scale;
  }
}

void bandLevels(const float* mag, int bands, float* out) {
  if (bands <= 0) return;
  if (bands > kMaxBands) bands = kMaxBands;
  if (bands != gEdgesBands) buildEdges(bands);

  for (int i = 0; i < bands; ++i) {
    float peak = 0.0f;
    for (int k = gEdges[i]; k < gEdges[i + 1]; ++k) {
      if (mag[k] > peak) peak = mag[k];
    }
    out[i] = peak;
  }
}

}  // namespace Fft