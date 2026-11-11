// Test máy tính cho apps/visualizer/fft (không cần board).
// Chạy từ thư mục gốc dự án:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tools/fft_test/fft_test.cpp src/apps/visualizer/fft.cpp -o fft_test && ./fft_test

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "apps/visualizer/fft.hpp"

static int gChecks = 0;
static int gFails = 0;

#define CHECK(cond, ...)                                  \
  do {                                                    \
    ++gChecks;                                            \
    if (!(cond)) {                                        \
      ++gFails;                                           \
      std::printf("  FAIL line %d: %s  ", __LINE__, #cond); \
      std::printf(__VA_ARGS__);                           \
      std::printf("\n");                                  \
    }                                                     \
  } while (0)

constexpr size_t N = Fft::kSize;
constexpr size_t B = Fft::kBins;
constexpr double kPi = 3.14159265358979323846;

static void makeSine(int16_t* dst, double bin, double amp /*0..1*/, double phase = 0.0) {
  for (size_t i = 0; i < N; ++i) {
    double v = amp * 32767.0 * std::sin(2.0 * kPi * bin * static_cast<double>(i) / N + phase);
    dst[i] = static_cast<int16_t>(std::lround(v));
  }
}

static size_t argMax(const float* m, size_t n) {
  size_t best = 0;
  for (size_t i = 1; i < n; ++i)
    if (m[i] > m[best]) best = i;
  return best;
}

static bool allFinite(const float* m, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!std::isfinite(m[i]) || m[i] < 0.0f) return false;
  return true;
}

static void testSilence() {
  std::printf("[im lang]\n");
  std::vector<int16_t> in(N, 0);
  std::vector<float> mag(B, -1.0f);
  Fft::magnitude(in.data(), mag.data());
  float mx = 0;
  for (float v : mag) mx = std::fmax(mx, v);
  CHECK(mx < 1e-6f, "max=%g", mx);

  // DC không đổi: cửa sổ Hann làm DC chỉ rò vào bin 0 và 1, các bin cao phải ~0.
  for (auto& s : in) s = 12000;
  Fft::magnitude(in.data(), mag.data());
  float hi = 0;
  for (size_t k = 3; k < B; ++k) hi = std::fmax(hi, mag[k]);
  CHECK(hi < 1e-4f, "DC ro vao bin cao: %g", hi);
}

static void testSinePeak() {
  std::printf("[sin dung tam bin]\n");
  const int bins[] = {1, 2, 5, 16, 40, 64, 100, 126};
  std::vector<int16_t> in(N);
  std::vector<float> mag(B);
  for (int k : bins) {
    makeSine(in.data(), k, 0.5);
    Fft::magnitude(in.data(), mag.data());
    size_t pk = argMax(mag.data(), B);
    CHECK(static_cast<int>(pk) == k, "bin %d: dinh o %zu", k, pk);
    CHECK(std::fabs(mag[pk] - 0.5f) < 0.02f, "bin %d: bien do %g (mong 0.5)", k, mag[pk]);
    // Hann: rò sang bin kề tối đa ~ nửa đỉnh, xa hơn 2 bin phải thấp.
    float far = 0;
    for (size_t j = 0; j < B; ++j)
      if (std::abs(static_cast<int>(j) - k) > 2) far = std::fmax(far, mag[j]);
    CHECK(far < 0.01f, "bin %d: ro xa %g", k, far);
    CHECK(allFinite(mag.data(), B), "bin %d: khong huu han", k);
  }

  std::printf("[pha khac nhau cho cung bien do]\n");
  for (double ph : {0.0, 0.7, 1.57, 3.0, 4.5}) {
    makeSine(in.data(), 30, 0.8, ph);
    Fft::magnitude(in.data(), mag.data());
    CHECK(static_cast<int>(argMax(mag.data(), B)) == 30, "pha %g lech dinh", ph);
    CHECK(std::fabs(mag[30] - 0.8f) < 0.03f, "pha %g: bien do %g", ph, mag[30]);
  }
}

static void testSineBetweenBins() {
  std::printf("[sin lech tam bin]\n");
  std::vector<int16_t> in(N);
  std::vector<float> mag(B);
  makeSine(in.data(), 20.5, 0.5);
  Fft::magnitude(in.data(), mag.data());
  size_t pk = argMax(mag.data(), B);
  CHECK(pk == 20 || pk == 21, "dinh o %zu", pk);
  // Scalloping loss của Hann tối đa khoảng 1,42 dB (~0,85).
  CHECK(mag[pk] > 0.38f && mag[pk] < 0.5f, "bien do %g", mag[pk]);
}

static void testTwoTones() {
  std::printf("[hai tan so]\n");
  std::vector<int16_t> a(N), b(N), in(N);
  std::vector<float> mag(B);
  makeSine(a.data(), 10, 0.3);
  makeSine(b.data(), 90, 0.4);
  for (size_t i = 0; i < N; ++i) in[i] = static_cast<int16_t>(a[i] + b[i]);
  Fft::magnitude(in.data(), mag.data());
  CHECK(std::fabs(mag[10] - 0.3f) < 0.02f, "bin 10: %g", mag[10]);
  CHECK(std::fabs(mag[90] - 0.4f) < 0.02f, "bin 90: %g", mag[90]);
}

static void testExtremes() {
  std::printf("[cuc tri, nhieu]\n");
  std::vector<int16_t> in(N);
  std::vector<float> mag(B);

  for (size_t i = 0; i < N; ++i) in[i] = (i & 1) ? 32767 : -32768;  // Nyquist, toàn thang
  Fft::magnitude(in.data(), mag.data());
  CHECK(allFinite(mag.data(), B), "Nyquist khong huu han");
  float mx = 0;
  for (float v : mag) mx = std::fmax(mx, v);
  CHECK(mx < 1.05f, "Nyquist vuot: %g", mx);

  for (auto& s : in) s = -32768;
  Fft::magnitude(in.data(), mag.data());
  CHECK(allFinite(mag.data(), B), "DC am cuc dai khong huu han");

  makeSine(in.data(), 50, 1.0);  // toàn thang
  Fft::magnitude(in.data(), mag.data());
  CHECK(std::fabs(mag[50] - 1.0f) < 0.03f, "toan thang: %g", mag[50]);

  std::srand(12345);
  for (int round = 0; round < 200; ++round) {
    for (auto& s : in) s = static_cast<int16_t>((std::rand() % 65536) - 32768);
    Fft::magnitude(in.data(), mag.data());
    if (!allFinite(mag.data(), B)) {
      CHECK(false, "nhieu vong %d khong huu han", round);
      break;
    }
  }
  CHECK(true, "ok");
}

static void testDeterministicAndInit() {
  std::printf("[lap lai, init nhieu lan]\n");
  Fft::init();
  Fft::init();
  std::vector<int16_t> in(N);
  std::vector<float> m1(B), m2(B);
  makeSine(in.data(), 33, 0.6);
  Fft::magnitude(in.data(), m1.data());
  // Chen một lần tính khác ở giữa để chắc bộ đệm tĩnh không giữ trạng thái cũ.
  std::vector<int16_t> other(N);
  makeSine(other.data(), 7, 0.9);
  std::vector<float> tmp(B);
  Fft::magnitude(other.data(), tmp.data());
  Fft::magnitude(in.data(), m2.data());
  bool same = true;
  for (size_t k = 0; k < B; ++k) same = same && (m1[k] == m2[k]);
  CHECK(same, "ket qua lan 2 khac lan 1");
}

static void testBandsImpulse() {
  std::printf("[gop dai: moi bin dung mot dai, don dieu]\n");
  const int bandCounts[] = {1, 2, 8, 16, 24, 32, 48, 63, 64};
  for (int bands : bandCounts) {
    std::vector<float> mag(B, 0.0f), out(bands);
    int lastBand = -1;
    std::vector<int> binsInBand(bands, 0);
    for (size_t k = 1; k < B; ++k) {
      std::fill(mag.begin(), mag.end(), 0.0f);
      mag[k] = 1.0f;
      Fft::bandLevels(mag.data(), bands, out.data());
      int hits = 0, which = -1;
      for (int i = 0; i < bands; ++i) {
        if (out[i] > 0.5f) {
          ++hits;
          which = i;
        }
      }
      CHECK(hits == 1, "bands=%d bin %zu: %d dai sang", bands, k, hits);
      if (hits == 1) {
        CHECK(which >= lastBand, "bands=%d bin %zu: dai lui %d -> %d", bands, k, lastBand, which);
        lastBand = which;
        ++binsInBand[which];
      }
    }
    for (int i = 0; i < bands; ++i)
      CHECK(binsInBand[i] >= 1, "bands=%d: dai %d khong co bin", bands, i);
    CHECK(lastBand == bands - 1, "bands=%d: dai cuoi la %d", bands, lastBand);

    // Bin 0 (DC) không được rơi vào dải nào.
    std::fill(mag.begin(), mag.end(), 0.0f);
    mag[0] = 1.0f;
    Fft::bandLevels(mag.data(), bands, out.data());
    float mx = 0;
    for (float v : out) mx = std::fmax(mx, v);
    CHECK(mx == 0.0f, "bands=%d: DC lot vao dai", bands);
  }

  std::printf("[thang log: dai thap hep hon dai cao]\n");
  {
    const int bands = 24;
    std::vector<float> mag(B, 0.0f), out(bands);
    std::vector<int> width(bands, 0);
    for (size_t k = 1; k < B; ++k) {
      std::fill(mag.begin(), mag.end(), 0.0f);
      mag[k] = 1.0f;
      Fft::bandLevels(mag.data(), bands, out.data());
      for (int i = 0; i < bands; ++i)
        if (out[i] > 0.5f) ++width[i];
    }
    CHECK(width[0] == 1, "dai 0 rong %d", width[0]);
    CHECK(width[bands - 1] > width[0] * 3, "dai cuoi rong %d", width[bands - 1]);
  }
}

static void testBandsSineEndToEnd() {
  std::printf("[sin -> dai]\n");
  const int bands = 32;
  std::vector<int16_t> in(N);
  std::vector<float> mag(B), out(bands);
  makeSine(in.data(), 100, 0.7);
  Fft::magnitude(in.data(), mag.data());
  Fft::bandLevels(mag.data(), bands, out.data());
  int pk = 0;
  for (int i = 1; i < bands; ++i)
    if (out[i] > out[pk]) pk = i;
  CHECK(pk >= bands - 6, "sin bin 100 roi vao dai %d/%d", pk, bands);
  CHECK(std::fabs(out[pk] - 0.7f) < 0.03f, "bien do dai %g", out[pk]);
  CHECK(out[0] < 0.01f, "dai thap khong im: %g", out[0]);
}

static void testBandsEdgeCases() {
  std::printf("[bands bien]\n");
  std::vector<float> mag(B, 0.5f);

  // bands = 0 và âm: không ghi gì.
  float canary[4] = {-7, -7, -7, -7};
  Fft::bandLevels(mag.data(), 0, canary);
  Fft::bandLevels(mag.data(), -5, canary);
  CHECK(canary[0] == -7 && canary[3] == -7, "bands<=0 van ghi");

  // bands vượt kMaxBands: chỉ ghi tối đa kMaxBands phần tử, không tràn.
  std::vector<float> big(Fft::kMaxBands + 8, -7.0f);
  Fft::bandLevels(mag.data(), 1000, big.data());
  for (int i = 0; i < Fft::kMaxBands; ++i)
    CHECK(big[i] == 0.5f, "dai %d = %g", i, big[i]);
  for (size_t i = Fft::kMaxBands; i < big.size(); ++i)
    CHECK(big[i] == -7.0f, "ghi tran o %zu", i);

  // Đổi số dải qua lại nhiều lần (bảng cache được dựng lại đúng).
  std::vector<float> a(24), b(48), a2(24);
  Fft::bandLevels(mag.data(), 24, a.data());
  Fft::bandLevels(mag.data(), 48, b.data());
  Fft::bandLevels(mag.data(), 24, a2.data());
  bool same = true;
  for (int i = 0; i < 24; ++i) same = same && (a[i] == a2[i]);
  CHECK(same, "doi so dai qua lai cho ket qua khac");
}

int main() {
  testSilence();
  testSinePeak();
  testSineBetweenBins();
  testTwoTones();
  testExtremes();
  testDeterministicAndInit();
  testBandsImpulse();
  testBandsSineEndToEnd();
  testBandsEdgeCases();
  std::printf("\n%d kiem tra, %d loi\n", gChecks, gFails);
  return gFails == 0 ? 0 : 1;
}