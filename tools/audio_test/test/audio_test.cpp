// Test may tinh cho services/audio_service.cpp (g++ + ASan + UBSan, phan cung bang stub).
// Cach chay: xem run.sh. Test #include thang audio_service.cpp de kiem tra ca ham noi bo (ringPush/ringPop...).
// Doi chieu: moi mau PCM ra I2S duoc so voi ket qua giai ma doc lap bang minimp3 (khong qua AudioService).
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "services/audio_service.cpp"   // NOLINT: white-box

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "test_hooks.hpp"

typedef std::vector<int16_t> V16;
typedef std::vector<uint8_t> VB;

// ---------------- khung test ----------------
static int g_pass = 0, g_fail = 0;
static std::vector<std::string> g_findings;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKF(c, ...) do { if (c) ++g_pass; else { ++g_fail; printf("  FAIL %s:%d: %s -> ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void finding(const char* id, bool bad, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void finding(const char* id, bool bad, const char* fmt, ...) {
  if (!bad) { ++g_pass; return; }
  char buf[512];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  std::string s = std::string(id) + ": " + buf;
  printf("  PHAT HIEN %s\n", s.c_str());
  g_findings.push_back(s);
}
static void section(const char* s) { printf("\n== %s ==\n", s); fflush(stdout); }

static double nowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
template <class F> static bool waitFor(F f, int timeoutMs) {
  double t0 = nowMs();
  while (!f()) {
    if (nowMs() - t0 > timeoutMs) return false;
    usleep(500);
  }
  return true;
}

// ---------------- file / tham chieu ----------------
static std::string g_root, g_data;
static VB readAll(const std::string& p) {
  VB v; FILE* f = fopen(p.c_str(), "rb");
  if (!f) return v;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  v.resize((size_t)n);
  if (n > 0 && fread(v.data(), 1, (size_t)n, f) != (size_t)n) v.clear();
  fclose(f); return v;
}
static void writeAll(const std::string& p, const VB& v) {
  FILE* f = fopen(p.c_str(), "wb");
  if (!f) { perror("writeAll"); exit(3); }
  if (!v.empty()) fwrite(v.data(), 1, v.size(), f);
  fclose(f);
}
static VB dataFile(const char* name) { return readAll(g_data + "/" + name); }
static std::string sdName(const char* name) { return std::string("/music/") + name; }

struct Ref {
  V16 mono; uint32_t hz = 0; int ch = 0; int kbps = 0; size_t fileBytes = 0; size_t dataStart = 0;
  double trueMs() const { return hz ? (double)mono.size() * 1000.0 / hz : 0; }
};
// Giai ma doc lap: tu parse ID3v2, goi minimp3 tren ca buffer, tron mono (L+R)/2.
static Ref refDecode(const VB& f) {
  Ref r; r.fileBytes = f.size();
  size_t pos = 0;
  if (f.size() >= 10 && memcmp(f.data(), "ID3", 3) == 0) {
    size_t sz = ((size_t)(f[6] & 0x7F) << 21) | ((size_t)(f[7] & 0x7F) << 14) | ((size_t)(f[8] & 0x7F) << 7) | (f[9] & 0x7F);
    pos = 10 + sz + ((f[5] & 0x10) ? 10 : 0);
  }
  r.dataStart = pos;
  mp3dec_t dec; mp3dec_init(&dec);
  static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  while (pos < f.size()) {
    mp3dec_frame_info_t info;
    int s = mp3dec_decode_frame(&dec, f.data() + pos, (int)(f.size() - pos), pcm, &info);
    if (info.frame_bytes == 0) break;
    pos += (size_t)info.frame_bytes;
    if (s <= 0) continue;
    if (!r.hz) { r.hz = (uint32_t)info.hz; r.ch = info.channels; r.kbps = info.bitrate_kbps; }
    for (int i = 0; i < s; i++)
      r.mono.push_back(info.channels >= 2 ? (int16_t)(((int)pcm[2 * i] + (int)pcm[2 * i + 1]) / 2) : pcm[i]);
  }
  return r;
}

// Chi muc de dinh vi mot cua so mau trong luong tham chieu
struct Idx {
  const V16* ref; std::unordered_multimap<uint64_t, size_t> m;
  static uint64_t key(const int16_t* p) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < 8; i++) h = (h ^ (uint16_t)p[i]) * 1099511628211ULL;
    return h;
  }
  explicit Idx(const V16* r) : ref(r) {
    if (r->size() >= 8) for (size_t i = 0; i + 8 <= r->size(); i++) m.insert(std::make_pair(key(r->data() + i), i));
  }
  // tra ve vi tri cua cua so (khop toan bo k mau) hoac -1
  long find(const int16_t* w, size_t k) const {
    if (k < 8) return -1;
    std::pair<std::unordered_multimap<uint64_t, size_t>::const_iterator, std::unordered_multimap<uint64_t, size_t>::const_iterator> rg = m.equal_range(key(w));
    for (std::unordered_multimap<uint64_t, size_t>::const_iterator it = rg.first; it != rg.second; ++it) {
      size_t p = it->second;
      if (p + k <= ref->size() && memcmp(ref->data() + p, w, k * sizeof(int16_t)) == 0) return (long)p;
    }
    return -1;
  }
};

// ---------------- tien ich dieu khien ----------------
static uint32_t g_lastRate = 44100;
static void quiesce() {
  AudioService::stop();
  waitFor([] { return AudioService::state() == AudioState::STOPPED && AudioService::positionMs() == 0 && AudioService::durationMs() == 0; }, 3000);
  usleep(20000);
  AudioService::setVolume(100);
  for (int i = 0; i < 50 && AudioService::finished(); i++) {}
  hooks::captureClear();
}
static void noViolations(const char* where) {
  CHECKF(hooks::lockOrderViolations == 0, "%s: sai thu tu khoa: %s", where, hooks::firstLockMessage().c_str());
  CHECKF(hooks::relockErrors == 0, "%s: khoa lai: %s", where, hooks::firstLockMessage().c_str());
}
// Phat den het bai; tra ve PCM trai.
static bool runToEnd(const char* name, int vol, V16* out, int timeoutMs, bool* finishedOnce = nullptr) {
  quiesce();
  AudioService::setVolume((uint8_t)vol);
  if (!AudioService::play(sdName(name).c_str())) return false;
  bool fin = waitFor([] { return AudioService::finished(); }, timeoutMs);
  if (finishedOnce) { usleep(200000); *finishedOnce = fin && !AudioService::finished(); }
  *out = hooks::captureLeft();
  return fin;
}
static bool sameStream(const V16& a, const V16& b, long* firstDiff) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) if (a[i] != b[i]) { *firstDiff = (long)i; return false; }
  *firstDiff = -1;
  return a.size() == b.size();
}

// ================= cac nhom test =================
static void testBeginConfig() {
  section("begin(): cau hinh I2S, task, goi lai");
  CHECK(AudioService::begin());
  CHECK(AudioService::begin());   // goi lai an toan
  std::vector<hooks::TaskRec> t = hooks::tasks();
  CHECKF(t.size() == 2, "tao %zu task (can 2)", t.size());
  if (t.size() == 2) {
    CHECK(t[0].name == "audio_dec" && t[0].stack == 32 * 1024 && t[0].prio == 3 && t[0].core == 0);
    CHECK(t[1].name == "audio_out" && t[1].stack == 4 * 1024 && t[1].prio == 4 && t[1].core == 0);
  }
  CHECK(hooks::i2sCfg.sample_rate == 44100 && hooks::i2sCfg.bits_per_sample == 16);
  CHECK(hooks::i2sCfg.dma_buf_count == 8 && hooks::i2sCfg.dma_buf_len == 256 && hooks::i2sCfg.tx_desc_auto_clear);
  CHECK(hooks::i2sPins.bck_io_num == 4 && hooks::i2sPins.ws_io_num == 5 && hooks::i2sPins.data_out_num == 6);
  CHECK(AudioService::state() == AudioState::STOPPED);
  CHECK(hooks::psramOutstanding == (long)(kRingSamples * 2 + kInCap));
}

static void testIntegrity() {
  section("Toan ven PCM: moi mau ra I2S khop giai ma doc lap (am luong 100)");
  const char* files[] = {"a_44k_stereo.mp3", "b_22k_mono.mp3", "c_48k_vbr.mp3", "d_8k_mono.mp3", "f_plain_44k.mp3"};
  for (size_t k = 0; k < sizeof(files) / sizeof(files[0]); k++) {
    Ref ref = refDecode(dataFile(files[k]));
    printf("  %s: %u Hz, %d kenh, %zu mau (%.0f ms)\n", files[k], ref.hz, ref.ch, ref.mono.size(), ref.trueMs());
    CHECK(ref.mono.size() > 1000);
    V16 got; bool once = false;
    bool fin = runToEnd(files[k], 100, &got, 20000, &once);
    CHECKF(fin, "%s: khong ket thuc", files[k]);
    long d;
    bool same = sameStream(got, ref.mono, &d);
    CHECKF(same, "%s: PCM khac tham chieu (got %zu, ref %zu, lech dau tien %ld)", files[k], got.size(), ref.mono.size(), d);
    CHECKF(hooks::captureLRMismatch == 0, "%s: %ld frame L!=R", files[k], (long)hooks::captureLRMismatch);
    CHECK(once);                                           // finished() dung 1 lan
    CHECK(AudioService::state() == AudioState::STOPPED);
    std::vector<uint32_t> rates = hooks::i2sRateChanges();
    if (ref.hz != g_lastRate) CHECKF(!rates.empty() && rates.back() == ref.hz, "%s: set_clk cuoi %u (can %u)", files[k], rates.empty() ? 0 : rates.back(), ref.hz);
    else CHECK(rates.empty() || rates.back() == ref.hz);
    g_lastRate = ref.hz;
    uint32_t dur = AudioService::durationMs(), pos = AudioService::positionMs();
    double err = ref.trueMs() > 0 ? (double)((long)dur - (long)ref.trueMs()) * 100.0 / ref.trueMs() : 0;
    printf("    thoi luong uoc %u ms, that %.0f ms (sai %.2f%%), vi tri cuoi %u ms\n", dur, ref.trueMs(), err, pos);
    double tolMs = std::max(ref.trueMs() * 0.03, 1.5 * (ref.hz >= 32000 ? 1152.0 : 576.0) * 1000.0 / ref.hz);   // Xing khong tinh frame header
    CHECKF(std::fabs((double)dur - ref.trueMs()) <= tolMs, "%s: thoi luong sai %.2f%% (%.0f ms)", files[k], err, (double)dur - ref.trueMs());
    CHECKF(pos <= dur, "%s: vi tri %u > thoi luong %u", files[k], pos, dur);
    CHECKF(pos + 60 >= (uint32_t)std::min<double>(ref.trueMs(), dur), "%s: vi tri cuoi %u xa thoi luong", files[k], pos);
  }
  noViolations("integrity");
}

static void testVolume() {
  section("Am luong: duong cong v^2, kep 0..100");
  VB f = dataFile("a_44k_stereo.mp3");
  Ref ref = refDecode(f);
  int vols[] = {0, 1, 30, 50, 99, 100, 255};
  for (size_t k = 0; k < sizeof(vols) / sizeof(vols[0]); k++) {
    int v = vols[k];
    quiesce();
    AudioService::setVolume((uint8_t)v);
    AudioService::play(sdName("a_44k_stereo.mp3").c_str());
    waitFor([] { return hooks::captureFrames() >= 30000; }, 8000);
    AudioService::stop();
    V16 got = hooks::captureLeft();
    int vv = v > 100 ? 100 : v;
    int32_t gain = (int32_t)((uint32_t)vv * vv * 256 / 10000);
    size_t n = std::min<size_t>(30000, got.size());
    long bad = 0, first = -1;
    for (size_t i = 0; i < n; i++) {
      int16_t exp = (int16_t)(((int32_t)ref.mono[i] * gain) >> 8);
      if (got[i] != exp) { if (first < 0) first = (long)i; bad++; }
    }
    CHECKF(n >= 30000 && bad == 0, "vol %d: %ld mau sai (dau tien %ld), n=%zu", v, bad, first, n);
  }
  noViolations("volume");
}

static void testPauseResumeStop() {
  section("Tam dung / tiep tuc / dung");
  Ref ref = refDecode(dataFile("n_long_44k.mp3"));
  quiesce();
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return hooks::captureFrames() >= 30000; }, 8000);
  int z0 = hooks::i2sZeroCalls;
  AudioService::pause();
  CHECK(AudioService::state() == AudioState::PAUSED);
  CHECKF(waitFor([&] { return hooks::i2sZeroCalls > z0; }, 1000), "pause() khong xoa bo dem DMA (se lap tieng cuoi)");
  usleep(150000);
  size_t s1 = hooks::captureFrames(); uint32_t p1 = AudioService::positionMs();
  int16_t tap[64];
  CHECK(AudioService::readPcmTap(tap, 64) == 0);          // PAUSED: khong co mau
  usleep(300000);
  CHECKF(hooks::captureFrames() == s1, "van ghi I2S khi PAUSED (%zu -> %zu)", s1, hooks::captureFrames());
  CHECK(AudioService::positionMs() == p1);
  AudioService::resume();
  CHECK(AudioService::state() == AudioState::PLAYING);
  waitFor([&] { return hooks::captureFrames() >= s1 + 30000; }, 8000);
  AudioService::stop();
  CHECK(AudioService::state() == AudioState::STOPPED);
  usleep(100000);
  size_t s2 = hooks::captureFrames();
  usleep(200000);
  CHECK(hooks::captureFrames() == s2);                     // het mau sau stop
  CHECK(!AudioService::finished());                        // stop khong phai "het bai"
  CHECK(waitFor([] { return AudioService::positionMs() == 0 && AudioService::durationMs() == 0; }, 2000));
  V16 got = hooks::captureLeft();
  long d; V16 refp(ref.mono.begin(), ref.mono.begin() + std::min(got.size(), ref.mono.size()));
  CHECKF(got.size() <= ref.mono.size() && std::equal(got.begin(), got.end(), ref.mono.begin()), "luong sau pause/resume khong lien tuc");
  (void)d; (void)refp;
  // pause khi khong phat, resume khi khong tam dung: khong doi trang thai
  AudioService::pause(); CHECK(AudioService::state() == AudioState::STOPPED);
  AudioService::resume(); CHECK(AudioService::state() == AudioState::STOPPED);
  noViolations("pause/resume/stop");
}

static void testPlaySwitch() {
  section("Doi bai khi dang phat: do doan mau cu con lot sang bai moi");
  Ref A = refDecode(dataFile("n_long_44k.mp3"));
  Ref B = refDecode(dataFile("c_48k_vbr.mp3"));
  Idx ib(&B.mono);
  std::vector<double> stale;
  for (int round = 0; round < 6; round++) {
    quiesce();
    AudioService::play(sdName("n_long_44k.mp3").c_str());
    waitFor([] { return hooks::captureFrames() >= 40000; }, 8000);
    size_t m = hooks::captureFrames();
    AudioService::play(sdName("c_48k_vbr.mp3").c_str());
    waitFor([&] { return hooks::captureFrames() >= m + 30000; }, 8000);
    V16 after = hooks::captureLeft(m);
    AudioService::stop();
    // tim diem bat dau cua bai B (cua so dau tien khop B tai vi tri 2000 de tranh doan lang dau bai)
    long pos = -1; size_t startIdx = 0;
    for (size_t i = 0; i + 64 < after.size() && i < 20000; i++) {
      long p = ib.find(after.data() + i, 64);
      if (p >= 2000 && (size_t)p <= i) { pos = p; startIdx = i - (size_t)p; break; }
    }
    CHECKF(pos >= 0, "khong tim thay bai moi trong luong ra");
    if (pos >= 0) stale.push_back((double)startIdx * 1000.0 / 44100.0);
  }
  double mx = 0, sum = 0;
  for (size_t i = 0; i < stale.size(); i++) { mx = std::max(mx, stale[i]); sum += stale[i]; }
  if (!stale.empty()) printf("  mau cu con phat sau play(): TB %.1f ms, toi da %.1f ms (%zu lan)\n", sum / stale.size(), mx, stale.size());
  finding("F-doi-bai-mau-cu", !stale.empty() && mx > 20.0,
          "play() khong xoa ring ngay: %.1f ms am cua bai cu con phat sau khi bam bai moi (toi da %.1f ms)", sum / (stale.empty() ? 1 : stale.size()), mx);
  noViolations("switch");
}

static long locateTail(const Idx& idx, const V16& cap, size_t k) {
  if (cap.size() < k + 8) return -1;
  return idx.find(cap.data() + cap.size() - k, k);
}

static void testSeek() {
  section("Tua (seek)");
  Ref ref = refDecode(dataFile("n_long_44k.mp3"));
  Idx idx(&ref.mono);
  double trueMs = ref.trueMs();
  quiesce();
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return AudioService::positionMs() >= 2000; }, 8000);
  uint32_t p0 = AudioService::positionMs();
  double tSeek0 = nowMs();
  AudioService::seek(+10);
  usleep(400000);
  double elapsed0 = (nowMs() - tSeek0) * hooks::timeScale;
  uint32_t p1 = AudioService::positionMs();
  V16 cap = hooks::captureLeft();
  long at = locateTail(idx, cap, 256);
  double tailMs = at < 0 ? -1 : (at + 256) * 1000.0 / 44100.0;
  printf("  +10 s: vi tri %u -> %u ms, mau cuoi o %.0f ms trong bai\n", p0, p1, tailMs);
  CHECKF(at >= 0, "luong sau seek khong phai doan lien tuc cua bai");
  CHECKF(std::fabs((double)p1 - ((double)p0 + 10000.0 + elapsed0)) < 900.0, "seek +10: %u -> %u (ky vong ~%.0f)", p0, p1, (double)p0 + 10000.0 + elapsed0);
  CHECKF(at >= 0 && std::fabs(tailMs - (double)p1) < 120.0, "vi tri bao %u ms nhung mau dang phat o %.0f ms", p1, tailMs);

  double tSeek = nowMs();
  AudioService::seek(-5);
  usleep(400000);
  uint32_t p2 = AudioService::positionMs();
  double elapsedSim = (nowMs() - tSeek) * hooks::timeScale;   // thoi gian mo phong troi qua (ms)
  cap = hooks::captureLeft(); at = locateTail(idx, cap, 256); tailMs = at < 0 ? -1 : (at + 256) * 1000.0 / 44100.0;
  printf("  -5 s: %u -> %u ms (mau cuoi o %.0f ms)\n", p1, p2, tailMs);
  CHECKF(std::fabs((double)p2 - ((double)p1 - 5000.0 + elapsedSim)) < 900.0, "seek -5: %u -> %u (ky vong ~%.0f)", p1, p2, (double)p1 - 5000.0 + elapsedSim);
  CHECKF(at >= 0 && std::fabs(tailMs - (double)p2) < 120.0, "sau seek -5 vi tri %u nhung mau o %.0f", p2, tailMs);

  AudioService::seek(-100);                                 // qua dau bai -> ve 0
  usleep(400000);
  uint32_t p3 = AudioService::positionMs();
  CHECKF(p3 < 400.0 * hooks::timeScale + 600.0, "seek -100 qua dau bai: vi tri %u", p3);
  CHECK(AudioService::state() == AudioState::PLAYING);

  // Tam dung roi tua: vi tri doi, khong ra am thanh, tiep tuc dung cho moi
  AudioService::seek(+4);
  usleep(300000);
  uint32_t pa = AudioService::positionMs();
  AudioService::pause();
  size_t cf = hooks::captureFrames();
  AudioService::seek(+3);
  usleep(400000);
  uint32_t pb = AudioService::positionMs();
  CHECKF(pb >= pa + 2500 && pb <= pa + 3600, "seek luc PAUSED: %u -> %u", pa, pb);
  CHECKF(hooks::captureFrames() <= cf + 600, "phat am thanh khi dang PAUSED");
  AudioService::resume();
  usleep(300000);
  cap = hooks::captureLeft(); at = locateTail(idx, cap, 256); tailMs = at < 0 ? -1 : (at + 256) * 1000.0 / 44100.0;
  CHECKF(at >= 0 && std::fabs(tailMs - (double)AudioService::positionMs()) < 150.0, "sau resume vi tri %u mau o %.0f", AudioService::positionMs(), tailMs);

  // Tua vuot cuoi bai: ket thuc dung quy trinh
  AudioService::seek(+1000);
  bool fin = waitFor([] { return AudioService::finished(); }, 6000);
  CHECK(fin);
  CHECK(AudioService::state() == AudioState::STOPPED);
  CHECKF(AudioService::positionMs() <= AudioService::durationMs(), "vi tri %u > thoi luong %u", AudioService::positionMs(), AudioService::durationMs());
  (void)trueMs;

  // Tua luc khong co bai: khong lam bai ke tiep bi lech
  quiesce();
  AudioService::seek(+5);
  usleep(100000);
  AudioService::play(sdName("a_44k_stereo.mp3").c_str());
  Ref ra = refDecode(dataFile("a_44k_stereo.mp3"));
  waitFor([] { return hooks::captureFrames() >= 20000; }, 8000);
  V16 g = hooks::captureLeft();
  CHECKF(g.size() >= 20000 && std::equal(g.begin(), g.begin() + 20000, ra.mono.begin()), "seek luc STOPPED lam bai ke tiep bat dau lech");
  AudioService::stop();

  // 300 lan tua lien tiep nhanh: khong hu, van phat tiep
  quiesce();
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return hooks::captureFrames() >= 20000; }, 8000);
  for (int i = 0; i < 300; i++) { AudioService::seek((i & 1) ? -2 : +2); usleep(1500); }
  usleep(300000);
  size_t c0 = hooks::captureFrames();
  usleep(300000);
  CHECKF(hooks::captureFrames() > c0 && AudioService::state() == AudioState::PLAYING, "ngung phat sau 300 lan tua");
  AudioService::stop();
  noViolations("seek");
}

static void testTap() {
  section("readPcmTap: cua so lien tuc, truoc am luong, nhieu luong doc");
  Ref ref = refDecode(dataFile("n_long_44k.mp3"));
  Idx idx(&ref.mono);
  int16_t buf[4096];
  CHECK(AudioService::readPcmTap(nullptr, 16) == 0);
  CHECK(AudioService::readPcmTap(buf, 0) == 0);
  quiesce();
  CHECK(AudioService::readPcmTap(buf, 64) == 0);          // STOPPED
  AudioService::setVolume(30);                             // tap phai khong phu thuoc am luong
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return hooks::captureFrames() >= 12000; }, 8000);
  size_t n = AudioService::readPcmTap(buf, 4096);
  CHECKF(n == 2048, "tra %zu mau (can 2048 = kTapSamples)", n);
  CHECKF(n > 0 && idx.find(buf, n) >= 0, "cua so tap khong khop luong giai ma (hoac bi nhan am luong)");
  size_t n2 = AudioService::readPcmTap(buf, 100);
  CHECK(n2 == 100);
  CHECK(idx.find(buf, n2) >= 0);
  // 3 luong doc lien tuc trong 1 giay
  std::atomic<int> bad(0), reads(0), stop(0);
  std::vector<std::thread> th;
  for (int t = 0; t < 3; t++) {
    th.push_back(std::thread([&, t] {
      int16_t b[2048]; std::mt19937 rng(t + 1);
      while (!stop) {
        size_t want = 64 + rng() % 1984;
        size_t got = AudioService::readPcmTap(b, want);
        reads++;
        if (got > 0 && got >= 8 && idx.find(b, got) < 0) bad++;
        usleep(200);
      }
    }));
  }
  usleep(1000000);
  stop = 1;
  for (size_t i = 0; i < th.size(); i++) th[i].join();
  CHECKF(bad == 0, "%d/%d cua so tap bi rach", (int)bad, (int)reads);
  CHECK(reads > 100);
  AudioService::stop();
  noViolations("tap");
}

static void testRingWhitebox() {
  section("Ring (white-box): quay vong, tran so 32 bit cua gHead/gTail");
  quiesce();
  std::mt19937 rng(7);
  const uint32_t starts[] = {0u, 65535u, 65536u - 100u, 0xFFFFFFFFu - 70000u, 0xFFFFFF00u, 0xFFFFFFFFu};
  for (size_t si = 0; si < sizeof(starts) / sizeof(starts[0]); si++) {
    xSemaphoreTake(gRingMtx, portMAX_DELAY);
    gHead = gTail = starts[si]; gSamplesOut = 0;
    xSemaphoreGive(gRingMtx);
    std::vector<int16_t> pushed, popped;
    int16_t seed = 0;
    for (int it = 0; it < 400; it++) {
      size_t canPush = ringFree();
      size_t n = 1 + rng() % 1152;
      if (n <= canPush) {
        int16_t tmp[1152];
        for (size_t i = 0; i < n; i++) { tmp[i] = seed; pushed.push_back(seed); seed = (int16_t)(seed * 31 + 7); }
        ringPush(tmp, n);
      }
      int16_t out[700];
      size_t got = ringPop(out, 1 + rng() % 700);
      popped.insert(popped.end(), out, out + got);
      CHECK((size_t)(gHead - gTail) <= kRingSamples);
    }
    int16_t out[700];
    for (;;) { size_t g = ringPop(out, 700); if (!g) break; popped.insert(popped.end(), out, out + g); }
    CHECKF(popped == pushed, "start=0x%08X: ring lam hong du lieu (%zu vs %zu mau)", starts[si], popped.size(), pushed.size());
    CHECK(ringFree() == kRingSamples);
  }
  // Day ring: ringFree ve 0, khong tran
  flushRing(0);
  int16_t big[1152] = {0};
  size_t pushedTotal = 0;
  while (ringFree() >= 1152) { ringPush(big, 1152); pushedTotal += 1152; }
  CHECK(ringFree() < 1152 && pushedTotal <= kRingSamples);
  flushRing(0);
  noViolations("ring");
}

static void makeVariants() {
  std::mt19937 rng(42);
  VB a = dataFile("a_44k_stereo.mp3"), p = dataFile("f_plain_44k.mp3");
  std::string m = g_root + "/music/";
  { VB v(a.begin(), a.begin() + a.size() * 4 / 10); writeAll(m + "x_trunc.mp3", v); }
  { VB v = a; size_t lo = v.size() * 3 / 10, hi = v.size() * 7 / 10; for (int i = 0; i < 400; i++) v[lo + rng() % (hi - lo)] = (uint8_t)rng(); writeAll(m + "x_corrupt.mp3", v); }
  { VB v = p; const char tag[] = "TAG"; v.insert(v.end(), tag, tag + 3); v.resize(v.size() + 125, 0); writeAll(m + "x_id3v1.mp3", v); }
  auto id3 = [](size_t sz, uint8_t ver, uint8_t flags) {
    VB h(10); memcpy(h.data(), "ID3", 3); h[3] = ver; h[4] = 0; h[5] = flags;
    h[6] = (sz >> 21) & 0x7F; h[7] = (sz >> 14) & 0x7F; h[8] = (sz >> 7) & 0x7F; h[9] = sz & 0x7F; return h;
  };
  { VB v = id3(100000, 3, 0); v.resize(10 + 100000, 0); v.insert(v.end(), p.begin(), p.end()); writeAll(m + "x_bigid3.mp3", v); }
  { VB v = id3(40000, 4, 0x10); v.resize(10 + 40000, 0); VB ft = id3(40000, 4, 0x10); memcpy(ft.data(), "3DI", 3); v.insert(v.end(), ft.begin(), ft.end()); v.insert(v.end(), p.begin(), p.end()); writeAll(m + "x_bigid3_footer.mp3", v); }
  { VB v(200000); for (size_t i = 0; i < v.size(); i++) v[i] = (uint8_t)(rng() & 0x7F); writeAll(m + "x_garbage.mp3", v); }
  writeAll(m + "x_empty.mp3", VB());
  writeAll(m + "x_id3only.mp3", id3(0, 3, 0));
  { VB v = id3(5000000, 3, 0); v.resize(1000, 0x11); writeAll(m + "x_id3claim.mp3", v); }
  { VB v(5); for (size_t i = 0; i < 5; i++) v[i] = (uint8_t)(rng() & 0x7F); writeAll(m + "x_short.mp3", v); }
  { VB v(p.begin(), p.begin() + 2400); writeAll(m + "x_tiny.mp3", v); }
  mkdir((m + "x_dir.mp3").c_str(), 0755);
}

static void testEdgeFiles() {
  section("File hong / bien: khong treo, khong crash, ket thuc dung");
  makeVariants();
  struct Case { const char* name; bool expectError; bool exactRef; };
  Case cases[] = {
    {"x_trunc.mp3", false, true}, {"x_id3v1.mp3", false, true}, {"x_bigid3.mp3", false, true},
    {"x_bigid3_footer.mp3", false, true}, {"x_corrupt.mp3", false, false}, {"x_garbage.mp3", false, false},
    {"x_short.mp3", false, false}, {"x_empty.mp3", true, false}, {"x_id3only.mp3", true, false},
    {"x_id3claim.mp3", true, false}, {"x_dir.mp3", true, false}, {"khong_co_file.mp3", true, false},
  };
  for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
    const Case& c = cases[k];
    quiesce();
    AudioService::play(sdName(c.name).c_str());
    if (c.expectError) {
      bool err = waitFor([] { return AudioService::state() == AudioState::ERROR; }, 3000);
      CHECKF(err, "%s: khong vao ERROR (state=%d)", c.name, (int)AudioService::state());
      CHECKF(!AudioService::finished(), "%s: ERROR nhung finished()=true", c.name);
      continue;
    }
    bool fin = waitFor([] { return AudioService::finished(); }, 15000);
    CHECKF(fin, "%s: khong ket thuc (treo?)", c.name);
    CHECK(AudioService::state() == AudioState::STOPPED);
    V16 got = hooks::captureLeft();
    VB raw = readAll(g_root + "/music/" + c.name);
    Ref ref = refDecode(raw);
    printf("  %s: %zu mau ra, tham chieu %zu\n", c.name, got.size(), ref.mono.size());
    if (c.exactRef) {
      long d; bool same = sameStream(got, ref.mono, &d);
      CHECKF(same, "%s: PCM khac tham chieu (got %zu ref %zu, lech %ld)", c.name, got.size(), ref.mono.size(), d);
    }
    if (strcmp(c.name, "x_corrupt.mp3") == 0) CHECKF(got.size() * 10 >= ref.mono.size() * 4, "x_corrupt: chi ra %zu/%zu mau", got.size(), ref.mono.size());
    if (strcmp(c.name, "x_garbage.mp3") == 0) CHECKF(got.size() == 0, "x_garbage: ra %zu mau", got.size());
    if (strstr(c.name, "bigid3") || strcmp(c.name, "x_id3v1.mp3") == 0) {
      double err = ref.trueMs() > 0 ? ((double)AudioService::durationMs() - ref.trueMs()) * 100.0 / ref.trueMs() : 0;
      printf("    thoi luong uoc %u ms, that %.0f ms (sai %.2f%%)\n", AudioService::durationMs(), ref.trueMs(), err);
      CHECKF(std::fabs(err) <= 3.0, "%s: thoi luong sai %.2f%%", c.name, err);
    }
  }
  // Duong dan bat thuong
  CHECK(!AudioService::play(nullptr));
  CHECK(!AudioService::play(""));
  std::string longPath = "/music/" + std::string(300, 'a') + ".mp3";
  quiesce();
  CHECK(AudioService::play(longPath.c_str()));
  CHECK(waitFor([] { return AudioService::state() == AudioState::ERROR; }, 3000));
  // Phuc hoi sau ERROR
  V16 got; Ref ra = refDecode(dataFile("a_44k_stereo.mp3"));
  bool fin = runToEnd("a_44k_stereo.mp3", 100, &got, 15000);
  long d; CHECK(fin && sameStream(got, ra.mono, &d));
  noViolations("edge");
}


static void testOpenTrackWhitebox() {
  section("openTrack (white-box): dataStart, Xing/Info theo cac kieu ID3");
  quiesce();
  Track* t = new Track();
  t->in = static_cast<uint8_t*>(Psram::alloc(kInCap));
  const char* names[] = {"a_44k_stereo.mp3", "b_22k_mono.mp3", "c_48k_vbr.mp3", "d_8k_mono.mp3", "f_plain_44k.mp3",
                         "n_long_44k.mp3", "x_bigid3.mp3", "x_bigid3_footer.mp3", "x_id3v1.mp3"};
  for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
    VB raw = readAll(g_root + "/music/" + names[k]);
    Ref ref = refDecode(raw);
    bool ok = openTrack(*t, sdName(names[k]).c_str());
    CHECKF(ok, "%s: openTrack that bai", names[k]);
    if (!ok) continue;
    CHECKF(t->dataStart == ref.dataStart, "%s: dataStart %u, dung la %zu", names[k], t->dataStart, ref.dataStart);
    CHECKF(t->dataBytes == raw.size() - ref.dataStart, "%s: dataBytes %u", names[k], t->dataBytes);
    bool hasXing = false;
    for (size_t i = ref.dataStart; i + 12 <= raw.size() && i < ref.dataStart + 256; i++)
      if (memcmp(&raw[i], "Xing", 4) == 0 || memcmp(&raw[i], "Info", 4) == 0) hasXing = true;
    CHECKF((t->xingFrames > 0) == hasXing, "%s: xingFrames=%u nhung hasXing=%d", names[k], t->xingFrames, (int)hasXing);
    closeTrack(*t);
  }
  Psram::release(t->in);
  delete t;
}

static void testEndOfTrackRace() {
  section("Het bai trung luc bam bai moi (cua so giua decode xong va output chuyen STOPPED)");
  Ref tiny = refDecode(readAll(g_root + "/music/x_tiny.mp3"));
  CHECK(tiny.mono.size() > 4000);
  std::mt19937 rng(5);
  int bad = 0, spuriousFinished = 0;
  const int trials = 150;
  for (int i = 0; i < trials; i++) {
    quiesce();
    AudioService::play(sdName("x_tiny.mp3").c_str());
    waitFor([] { return (gDecodeDone && gHead == gTail) || AudioService::state() != AudioState::PLAYING; }, 3000);   // ring da can: output sap cho 80 ms
    usleep((useconds_t)(rng() % (uint32_t)(95000.0 / hooks::timeScale)));   // 0..95 ms mo phong, phu het cua so cho 80 ms
    size_t m = hooks::captureFrames();
    AudioService::play(sdName("a_44k_stereo.mp3").c_str());
    bool ok = waitFor([&] { return hooks::captureFrames() >= m + 8000; }, 3000);
    if (!ok || AudioService::state() != AudioState::PLAYING) bad++;
    if (ok && AudioService::finished()) spuriousFinished++;
  }
  CHECKF(bad == 0, "%d/%d lan bai moi bi tat/khong phat do cuoi bai cu danh dau STOPPED", bad, trials);
  CHECKF(spuriousFinished == 0, "%d lan finished() bao nham sau khi da bam bai moi", spuriousFinished);
  quiesce();
}

static void testSdFaults() {
  section("Loi SD khi dang phat");
  Ref ra = refDecode(dataFile("n_long_44k.mp3"));
  // 1) the bi rut: doc tra 0 sau ~60 KB
  hooks::sdFailReadAfter = 60000;
  V16 got; bool fin = runToEnd("n_long_44k.mp3", 100, &got, 15000);
  hooks::sdFailReadAfter = -1;
  CHECKF(fin, "mat the giua bai: khong ket thuc");
  CHECK(AudioService::state() == AudioState::STOPPED);
  CHECKF(got.size() > 20000 && got.size() < ra.mono.size() / 2, "mat the: ra %zu mau", got.size());
  CHECK(std::equal(got.begin(), got.end(), ra.mono.begin()));
  // 2) the bi thao (isMounted false) luc bam play
  hooks::sdMounted = 0;
  quiesce();
  AudioService::play(sdName("a_44k_stereo.mp3").c_str());
  CHECK(waitFor([] { return AudioService::state() == AudioState::ERROR; }, 3000));
  hooks::sdMounted = 1;
  // 3) the cham (20 ms/lan doc): van phat dung noi dung, chi bi doi
  hooks::sdReadDelayUs = 20000;
  Ref rb = refDecode(dataFile("a_44k_stereo.mp3"));
  fin = runToEnd("a_44k_stereo.mp3", 100, &got, 20000);
  hooks::sdReadDelayUs = 0;
  long d;
  CHECKF(fin && sameStream(got, rb.mono, &d), "the cham: noi dung sai (lech %ld)", d);
  // 4) rut the roi cam lai giua bai
  quiesce();
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return hooks::captureFrames() >= 20000; }, 8000);
  hooks::sdMounted = 0; usleep(300000); hooks::sdMounted = 1;
  AudioService::stop();
  CHECK(AudioService::state() == AudioState::STOPPED);
  noViolations("sd");
}

static void testRingFullAndMonotonic() {
  section("Ring day, vi tri khong lui");
  quiesce();
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return hooks::captureFrames() >= 8000; }, 8000);
  AudioService::pause();                    // de decode dien day ring
  usleep(800000);
  uint32_t fill = (uint32_t)(gHead - gTail);
  CHECKF(fill <= kRingSamples, "ring day vuot dung luong: %u", fill);
  CHECKF(fill >= kRingSamples - kMaxFrameSamples, "ring khong duoc dien day (%u/%zu)", fill, kRingSamples);
  AudioService::resume();
  uint32_t last = 0; bool mono = true; uint32_t dur = AudioService::durationMs();
  double t0 = nowMs();
  while (nowMs() - t0 < 1500) {
    uint32_t p = AudioService::positionMs();
    if (p < last) mono = false;
    if (dur && p > dur) mono = false;
    last = p; usleep(2000);
  }
  CHECK(mono);
  AudioService::stop();
  noViolations("ring-full");
}

static void testStress() {
  section("Phim bam ngau nhien tu 4 luong (ASan/UBSan, kiem tra khoa, khong treo)");
  const char* names[] = {"a_44k_stereo.mp3", "b_22k_mono.mp3", "c_48k_vbr.mp3", "d_8k_mono.mp3", "n_long_44k.mp3",
                         "x_trunc.mp3", "x_corrupt.mp3", "x_garbage.mp3", "x_short.mp3", "x_empty.mp3", "khong_co.mp3"};
  const int nn = sizeof(names) / sizeof(names[0]);
  quiesce();
  std::atomic<int> stop(0), done(0);
  std::atomic<long> ops(0);
  std::vector<std::thread> th;
  for (int t = 0; t < 4; t++) {
    th.push_back(std::thread([&, t] {
      std::mt19937 rng(100 + t);
      int16_t b[2048];
      while (!stop) {
        switch (rng() % 12) {
          case 0: case 1: AudioService::play(sdName(names[rng() % nn]).c_str()); break;
          case 2: AudioService::stop(); break;
          case 3: AudioService::pause(); break;
          case 4: AudioService::resume(); break;
          case 5: AudioService::seek((int)(rng() % 61) - 30); break;
          case 6: AudioService::setVolume((uint8_t)(rng() % 120)); break;
          case 7: (void)AudioService::state(); break;
          case 8: (void)AudioService::positionMs(); (void)AudioService::durationMs(); break;
          case 9: (void)AudioService::finished(); break;
          default: (void)AudioService::readPcmTap(b, rng() % 3000); break;
        }
        ops++;
        usleep(rng() % 3000);
      }
      done++;
    }));
  }
  usleep(4000000);
  stop = 1;
  bool joined = waitFor([&] { return done == 4; }, 20000);
  if (!joined) { printf("  TREO: luong khong thoat sau 20 s (nghi deadlock)\n"); fflush(stdout); _exit(2); }
  for (size_t i = 0; i < th.size(); i++) th[i].join();
  printf("  %ld thao tac\n", (long)ops);
  CHECK(ops > 500);
  noViolations("stress");
  // Sau bao ap luc, module van phat dung
  Ref ra = refDecode(dataFile("a_44k_stereo.mp3"));
  V16 got; bool fin = runToEnd("a_44k_stereo.mp3", 100, &got, 20000);
  long d; CHECKF(fin && sameStream(got, ra.mono, &d), "sau stress phat sai (lech %ld)", d);
  CHECK(hooks::psramOutstanding == (long)(kRingSamples * 2 + kInCap));   // khong ro PSRAM
}

// ---------------- kich ban chay rieng tung tien trinh ----------------
static int scenarioPreBegin() {
  section("Goi ham truoc begin(): khong crash, tra gia tri an toan");
  CHECK(AudioService::state() == AudioState::STOPPED);
  CHECK(!AudioService::play("/music/a_44k_stereo.mp3"));
  AudioService::stop(); AudioService::seek(5); AudioService::pause(); AudioService::resume(); AudioService::setVolume(30);
  CHECK(AudioService::positionMs() == 0);
  CHECK(AudioService::durationMs() == 0);
  CHECK(!AudioService::finished());
  int16_t b[16];
  CHECK(AudioService::readPcmTap(b, 16) == 0);
  return 0;
}
static int scenarioBeginPsramFail() {
  section("begin() khi het PSRAM");
  for (int nth = 1; nth <= 2; nth++) {
    hooks::psramResetCounter();
    hooks::psramFailNth = nth;
    bool r = AudioService::begin();
    CHECKF(!r, "cap phat thu %d that bai nhung begin() tra true", nth);
    CHECKF(hooks::psramOutstanding == 0, "that bai o lan cap phat %d: con giu %ld byte", nth, (long)hooks::psramOutstanding);
    CHECK(hooks::tasks().empty());
    hooks::psramFailNth = 0;
  }
  CHECK(AudioService::begin());                             // phuc hoi duoc
  CHECK(hooks::tasks().size() == 2);
  Ref ra = refDecode(dataFile("a_44k_stereo.mp3"));
  V16 got; bool fin = runToEnd("a_44k_stereo.mp3", 100, &got, 15000);
  long d; CHECKF(fin && sameStream(got, ra.mono, &d), "phuc hoi sau het PSRAM: phat sai");
  return 0;
}
static int scenarioBeginI2sFail() {
  section("begin() khi I2S loi (goi lai nhieu lan, nhu MusicApp::onCreate)");
  hooks::i2sInstallFail = 1;
  bool r = AudioService::begin();
  CHECK(!r);
  long after1 = hooks::psramOutstanding;
  for (int i = 0; i < 5; i++) CHECK(!AudioService::begin());
  long after6 = hooks::psramOutstanding;
  printf("  PSRAM giu sau 1 lan loi: %ld B, sau 6 lan: %ld B\n", after1, after6);
  CHECKF(after1 == 0 && after6 == 0, "begin() that bai o i2s_driver_install con giu PSRAM: %ld B sau 1 lan, %ld B sau 6 lan", after1, after6);
  CHECK(hooks::tasks().empty());
  hooks::i2sInstallFail = 0;
  CHECK(AudioService::begin());
  return 0;
}
// begin() that bai o i2s_set_pin (driver da cai): phai go driver va tra bo nho, goi lai phai thanh cong.
static int scenarioBeginPinFail() {
  section("begin() khi i2s_set_pin loi (driver da cai)");
  long base = hooks::psramOutstanding;
  hooks::i2sPinFail = 1;
  int un0 = hooks::i2sUninstallCalls;
  for (int i = 0; i < 6; i++) CHECK(!AudioService::begin());
  CHECKF(hooks::psramOutstanding == base, "set_pin loi: giu PSRAM %ld B (mong doi 0)", hooks::psramOutstanding - base);
  CHECKF(hooks::i2sUninstallCalls - un0 == 6, "set_pin loi: go driver %d lan (mong doi 6)", hooks::i2sUninstallCalls - un0);
  CHECK(hooks::tasks().empty());
  hooks::i2sPinFail = 0;
  CHECK(AudioService::begin());                  // driver da duoc go nen cai lai duoc
  CHECK(hooks::tasks().size() == 2);
  return 0;
}
// seek(): gia tri cuc doan khong gay tran so, bam lien tuc khong gay tran cong don.
// finished(): hai luong doc dong thoi, moi lan het bai chi duoc bao dung mot lan, khong mat, khong nhan doi.
static int scenarioFinishedRace() {
  section("finished(): hai luong doc dong thoi, N bai -> dung N lan bao");
  CHECK(AudioService::begin());
  quiesce();
  AudioService::setVolume(100);
  std::atomic<int> total(0);
  std::atomic<bool> stopFlag(false);
  auto reader = [&] { while (!stopFlag.load()) { if (AudioService::finished()) ++total; usleep(20); } };
  std::thread t1(reader), t2(reader);
  const int N = 12;
  int got = 0;
  for (int i = 0; i < N; i++) {
    AudioService::play(sdName("d_8k_mono.mp3").c_str());
    bool ok = waitFor([&] { return total.load() >= i + 1; }, 20000);
    CHECKF(ok, "bai %d khong thay bao het bai", i);
    got = total.load();
  }
  usleep(300000);
  stopFlag = true; t1.join(); t2.join();
  CHECKF(total.load() == N, "bao het bai %d lan, mong doi %d", total.load(), N);
  (void)got;
  return 0;
}
static int scenarioSeekExtreme() {
  section("seek() voi gia tri cuc doan (UBSan)");
  CHECK(AudioService::begin());
  quiesce();
  AudioService::play(sdName("n_long_44k.mp3").c_str());
  waitFor([] { return hooks::captureFrames() >= 20000; }, 8000);
  AudioService::seek(INT_MAX);
  usleep(200000);
  AudioService::seek(INT_MIN);
  usleep(200000);
  AudioService::seek(2147483);
  AudioService::seek(-2147483);
  for (int i = 0; i < 2000; i++) AudioService::seek(INT_MAX);   // cong don lien tuc phai bao hoa, khong tran
  for (int i = 0; i < 2000; i++) AudioService::seek(INT_MIN);
  usleep(200000);
  CHECK(AudioService::state() == AudioState::PLAYING || AudioService::state() == AudioState::STOPPED);
  AudioService::stop();
  return 0;
}

static int runMain() {
  testBeginConfig();
  testIntegrity();
  testVolume();
  testPauseResumeStop();
  testPlaySwitch();
  testSeek();
  testTap();
  testRingWhitebox();
  testEdgeFiles();
  testOpenTrackWhitebox();
  testEndOfTrackRace();
  testSdFaults();
  testRingFullAndMonotonic();
  testStress();
  noViolations("tong");
  return 0;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc < 3) { fprintf(stderr, "dung: audio_test <thu_muc_data> <kich_ban> [timeScale]\n"); return 64; }
  g_data = argv[1];
  std::string sc = argv[2];
  if (argc > 3) hooks::timeScale = atof(argv[3]);
  char tmpl[] = "/tmp/audio_test_XXXXXX";
  if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 3; }
  g_root = tmpl;
  mkdir((g_root + "/music").c_str(), 0755);
  const char* base[] = {"a_44k_stereo.mp3", "b_22k_mono.mp3", "c_48k_vbr.mp3", "d_8k_mono.mp3", "f_plain_44k.mp3", "n_long_44k.mp3"};
  for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); i++) {
    VB v = dataFile(base[i]);
    if (v.empty()) { fprintf(stderr, "thieu file %s/%s (chay gen_mp3.sh)\n", g_data.c_str(), base[i]); return 3; }
    writeAll(g_root + "/music/" + base[i], v);
  }
  hooks::sdSetRoot(g_root);
  int rc = 0;
  if (sc == "pre_begin") rc = scenarioPreBegin();
  else if (sc == "begin_psram_fail") rc = scenarioBeginPsramFail();
  else if (sc == "begin_i2s_fail") rc = scenarioBeginI2sFail();
  else if (sc == "seek_extreme") rc = scenarioSeekExtreme();
  else if (sc == "begin_pin_fail") rc = scenarioBeginPinFail();
  else if (sc == "finished_race") rc = scenarioFinishedRace();
  else if (sc == "main") rc = runMain();
  else { fprintf(stderr, "kich ban la: %s\n", sc.c_str()); rc = 64; }
  noViolations(sc.c_str());
  printf("\n[%s] dat %d, hong %d, phat hien %zu\n", sc.c_str(), g_pass, g_fail, g_findings.size());
  for (size_t i = 0; i < g_findings.size(); i++) printf("  * %s\n", g_findings[i].c_str());
  fflush(stdout);
  std::string cmd = "rm -rf " + g_root; int ignored = system(cmd.c_str()); (void)ignored;
  _exit(rc ? rc : (g_fail ? 1 : 0));
}
