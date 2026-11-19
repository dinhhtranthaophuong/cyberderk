// tools/perf_test/perf_test.cpp
// Sketch đo hiệu năng cho firmware Cyberdeck (NHÓM C). Build bằng env `perf_test`
// (xem README_perf.md). KHÔNG thuộc firmware chính. Không bấm phím khi đang chạy.
//
// PHẦN 1 (VISIT, không nhạc) và PHẦN 2 (VISIT, có nhạc nền): với từng app
// (Pet, Album, Music, Visualizer, Settings) lặp kReps lần Main -> app -> Main và đo:
//   - to_ms     : từ lúc gọi goTo() đến lúc AppManager báo đã sang app (độ phân giải ~5 ms)
//   - back_ms   : từ lúc gọi goHome() đến lúc về Main
//   - tick_max  : lần AppManager::tick() chặn loop() lâu nhất trong lúc chuyển (ms)
//   - psram_in / heap_in : bộ nhớ trống ở cuối thời gian ở lại (kDwellMs) so với Main ngay trước đó
//                  => "foot_psram/foot_heap" = app chiếm bao nhiêu byte khi đang mở
//   - psram_after : PSRAM trống sau khi về Main và chờ kSettleMs, so với trước khi vào (rò rỉ)
//   - stack còn trống tối thiểu (high-water mark) của MỌI task, gộp theo tên, lấy mẫu lúc đang ở trong app
//   - đỉnh dùng bộ nhớ từ lúc khởi động (heap_caps_get_minimum_free_size) sau mỗi app
// PHẦN 3 (SOAK): chạy lâu kSoakMs, nhạc nền phát lặp, các hành động ngẫu nhiên xác định
//   (vào app ở lại 1-5 s, thoát nhanh, đổi theme đủ vòng, đứng yên ở Main). Mỗi kSoakSampleMs
//   (và chỉ ở Main, đã chờ kSettleMs) lấy một mẫu; kết luận dựa trên mẫu từ lúc hết warm-up:
//   chênh lệch + độ dốc bộ nhớ trống, số task, dứt tiếng, loop() bị chặn lâu nhất.
//
// Mọi dòng dữ liệu bắt đầu bằng "PERF," (dán nguyên log cuối cùng là đủ).
//
// Cờ build:
//   -DPERF_SKIP_VISITS=1   bỏ phần 1 + 2
//   -DPERF_SKIP_SOAK=1     bỏ phần 3
//   -DPERF_SOAK_MIN=30     thời gian soak (phút), mặc định 30
//   -DPERF_REPS=8          số lần mỗi app trong phần 1 và 2, mặc định 8
//   -DPERF_NO_TASKSTATE=1  không đọc stack từng task (nếu FreeRTOS không bật trace facility)

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <string>
#include <vector>

#include "core/app_manager.hpp"
#include "core/lvgl_port.hpp"
#include "core/psram.hpp"
#include "core/theme.hpp"
#include "services/audio_service.hpp"
#include "services/input_service.hpp"
#include "services/sd_storage.hpp"
#include "services/settings_store.hpp"
#include "services/time_service.hpp"

#ifndef PERF_SOAK_MIN
#define PERF_SOAK_MIN 30
#endif
#ifndef PERF_REPS
#define PERF_REPS 8
#endif

namespace {

// ---------- Cấu hình ----------
constexpr uint32_t kBootWaitMs = 10000;
constexpr uint32_t kDwellMs = 3000;        // ở trong app đủ lâu để dựng xong (Music 45 khung đĩa ~ 6 s: xem kDwellMusicMs)
constexpr uint32_t kDwellMusicMs = 7000;
constexpr uint32_t kSettleMs = 2000;
constexpr uint32_t kSwitchTimeoutMs = 3000;
constexpr uint32_t kAudioPrimeMs = 1500;
constexpr int kReps = PERF_REPS;
constexpr int kWarmReps = 2;               // các lần đầu tính là "nguội", báo riêng
constexpr uint32_t kSoakMs = (uint32_t)PERF_SOAK_MIN * 60u * 1000u;
constexpr uint32_t kSoakSampleMs = 60u * 1000u;
constexpr int kSoakWarmSamples = 2;        // bỏ 2 mẫu đầu của soak
constexpr int kMaxSoakSamples = 128;
constexpr int32_t kTol = 512;              // dao động chấp nhận được (byte)
constexpr size_t kTrackLen = 160;
constexpr uint32_t kWinMs = 500;
constexpr double kStallRatio = 0.85;
constexpr int kMaxTasks = 40;

struct AppDef {
  AppId id;
  const char* name;
  uint32_t dwell;
};
constexpr AppDef kApps[] = {
    {AppId::APP_PET, "pet", kDwellMs},
    {AppId::APP_ALBUM, "album", kDwellMs},
    {AppId::APP_MUSIC_PLAYER, "music", kDwellMusicMs},
    {AppId::APP_VISUALIZER, "visualizer", kDwellMs},
    {AppId::APP_SETTINGS, "settings", kDwellMs},
};
constexpr int kNumApps = sizeof(kApps) / sizeof(kApps[0]);

// ---------- Mẫu bộ nhớ ----------
struct Mem {
  uint32_t heapFree, heapLargest, psramFree, psramLargest, tasks;
};
Mem takeMem() {
  Mem m;
  m.heapFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  m.heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  m.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  m.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  m.tasks = uxTaskGetNumberOfTasks();
  return m;
}

double slopeOf(const uint32_t* y, int n) {
  if (n < 2) return 0.0;
  double sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (int i = 0; i < n; ++i) {
    sx += i;
    sy += y[i];
    sxy += (double)i * y[i];
    sxx += (double)i * i;
  }
  const double den = n * sxx - sx * sx;
  return den == 0.0 ? 0.0 : (n * sxy - sx * sy) / den;
}

// ---------- Stack từng task (gộp theo tên, giữ giá trị nhỏ nhất đã thấy) ----------
struct TaskMin {
  char name[configMAX_TASK_NAME_LEN];
  uint32_t hwmMin;
  bool used;
};
TaskMin g_taskMin[kMaxTasks];

// Tên task thử tra khi không có uxTaskGetSystemState (thêm tên khác: -DPERF_TASK_NAMES='"a","b"').
#ifndef PERF_TASK_NAMES
#define PERF_TASK_NAMES "loopTask", "lvgl", "LVGL", "lv_task", "audio", "audio_dec", "audio_out", "audio_task", "album", \
                        "disc_cover", "slideshow", "Tmr Svc", "esp_timer", "ipc0", "ipc1", "IDLE0", "IDLE1"
#endif

void noteStack(const char* name, uint32_t hwm) {
  int slot = -1;
  for (int k = 0; k < kMaxTasks; ++k) {
    if (g_taskMin[k].used && strncmp(g_taskMin[k].name, name, configMAX_TASK_NAME_LEN) == 0) { slot = k; break; }
  }
  if (slot < 0) {
    for (int k = 0; k < kMaxTasks; ++k) {
      if (!g_taskMin[k].used) {
        slot = k;
        g_taskMin[k].used = true;
        strncpy(g_taskMin[k].name, name, configMAX_TASK_NAME_LEN - 1);
        g_taskMin[k].name[configMAX_TASK_NAME_LEN - 1] = '\0';
        g_taskMin[k].hwmMin = UINT32_MAX;
        break;
      }
    }
  }
  if (slot >= 0 && hwm < g_taskMin[slot].hwmMin) g_taskMin[slot].hwmMin = hwm;
}

void sampleTaskStacks() {
#ifndef PERF_NO_TASKSTATE
  static TaskStatus_t st[kMaxTasks];
  const UBaseType_t n = uxTaskGetSystemState(st, kMaxTasks, nullptr);
  for (UBaseType_t i = 0; i < n; ++i) noteStack(st[i].pcTaskName, st[i].usStackHighWaterMark);
#else
  static const char* const names[] = {PERF_TASK_NAMES};
  for (const char* nm : names) {
    TaskHandle_t h = xTaskGetHandle(nm);
    if (h) noteStack(nm, uxTaskGetStackHighWaterMark(h));
  }
#endif
}

void printTaskStacks(const char* when) {
  int cnt = 0;
  for (int k = 0; k < kMaxTasks; ++k) {
    if (!g_taskMin[k].used) continue;
    ++cnt;
    Serial.printf("PERF,TASK,%s,%s,stack_con_trong_min_B=%lu\n", when, g_taskMin[k].name,
                  (unsigned long)g_taskMin[k].hwmMin);
  }
#ifdef PERF_NO_TASKSTATE
  Serial.printf("PERF,TASK,%s,so_task_tra_duoc=%d,(chi_tra_theo_ten_PERF_TASK_NAMES),tong_task_hien_co=%u\n", when, cnt,
                (unsigned)uxTaskGetNumberOfTasks());
#endif
}

// ---------- Nhạc nền + đo dứt tiếng ----------
struct AudioMon {
  bool valid;
  uint32_t winStart, lastPos;
  int windows, stalls, skipped, restarts, playFail;
  double minRatio;
  uint32_t gapMax, lastTick;
};
AudioMon g_mon;
bool g_audioActive = false;
bool g_audioRestart = true;
uint32_t g_audioStart = 0, g_lastRestart = 0;
char g_track[kTrackLen];

void resetMon() { memset(&g_mon, 0, sizeof(g_mon)); g_mon.minRatio = 9.9; }

bool startAudio(uint32_t now) {
  if (!AudioService::begin()) return false;
  const std::vector<std::string> files = SdStorage::listFiles("/music", ".mp3");
  if (files.empty()) return false;
  strncpy(g_track, files[0].c_str(), kTrackLen - 1);
  g_track[kTrackLen - 1] = '\0';
  if (!AudioService::play(g_track)) return false;
  resetMon();
  g_audioActive = true;
  g_audioRestart = true;
  g_audioStart = now;
  g_lastRestart = now;
  Serial.printf("PERF,AUDIO,phat,%s\n", g_track);
  return true;
}

void stopAudio() {
  if (!g_audioActive) return;
  AudioService::stop();
  g_audioActive = false;
}

// Đọc millis() NGAY ở đây (xem leak_test.cpp: mốc thời gian cũ gây dứt tiếng giả).
void audioTick() {
  if (!g_audioActive) return;
  const uint32_t now = millis();
  if (g_mon.lastTick != 0) {
    const uint32_t gap = now - g_mon.lastTick;
    if (gap > g_mon.gapMax) g_mon.gapMax = gap;
  }
  g_mon.lastTick = now;
  const AudioState st = AudioService::state();
  if (st == AudioState::STOPPED || st == AudioState::ERROR) {
    g_mon.valid = false;
    if (g_audioRestart && now - g_lastRestart >= 300) {
      g_lastRestart = now;
      if (AudioService::play(g_track)) {
        ++g_mon.restarts;
        g_audioStart = now;
      } else if (++g_mon.playFail >= 5) {
        g_audioRestart = false;
      }
    }
    return;
  }
  if (now - g_audioStart < kAudioPrimeMs) {
    g_mon.valid = false;
    return;
  }
  const uint32_t pos = AudioService::positionMs();
  if (!g_mon.valid) {
    g_mon.valid = (st == AudioState::PLAYING);
    g_mon.winStart = now;
    g_mon.lastPos = pos;
    return;
  }
  const uint32_t dt = now - g_mon.winStart;
  if (dt < kWinMs) return;
  if (st == AudioState::PLAYING && pos >= g_mon.lastPos) {
    const double ratio = (double)(pos - g_mon.lastPos) / (double)dt;
    ++g_mon.windows;
    if (ratio < g_mon.minRatio) g_mon.minRatio = ratio;
    if (ratio < kStallRatio) {
      ++g_mon.stalls;
      if (g_mon.stalls <= 10) {
        Serial.printf("PERF,AUDIO,dut,t_ms=%lu,ratio=%.2f,app=%d\n", (unsigned long)now, ratio,
                      (int)AppManager::current());
      }
    }
  } else {
    ++g_mon.skipped;
  }
  g_mon.winStart = now;
  g_mon.lastPos = pos;
  g_mon.valid = (st == AudioState::PLAYING);
}

// ---------- Độ chặn của AppManager::tick() ----------
uint32_t g_tickMaxUs = 0;     // lần tick lâu nhất kể từ lần reset gần nhất
uint32_t g_tickMaxAllUs = 0;  // lâu nhất cả chương trình

// ---------- Thống kê từng app (phần 1 và 2) ----------
struct AppStat {
  uint32_t toMin, toMax, toSum, backMin, backMax, backSum;
  uint32_t toCold, backCold;
  uint32_t tickToMaxUs, tickBackMaxUs;
  int32_t footPsramMax, footHeapMax;   // lớn nhất trong các lần đo (byte chiếm khi đang mở)
  int32_t footPsramMin, footHeapMin;
  int32_t afterMin;                    // psram_after - psram_before, nhỏ nhất (âm = hụt)
  uint32_t largestInMin;               // khối PSRAM trống lớn nhất nhỏ nhất khi đang mở
  uint32_t heapLargestInMin;           // khối heap nội bộ lớn nhất, nhỏ nhất khi đang mở
  int n, fail;
};
AppStat g_stat[2][kNumApps];
uint32_t g_peakPsramMin[2][kNumApps];  // heap_caps_get_minimum_free_size(SPIRAM) sau app
uint32_t g_peakHeapMin[2][kNumApps];

void resetStats() {
  for (int m = 0; m < 2; ++m) {
    for (int a = 0; a < kNumApps; ++a) {
      AppStat& s = g_stat[m][a];
      memset(&s, 0, sizeof(s));
      s.toMin = s.backMin = UINT32_MAX;
      s.footPsramMin = s.footHeapMin = INT32_MAX;
      s.footPsramMax = s.footHeapMax = INT32_MIN;
      s.afterMin = INT32_MAX;
      s.largestInMin = s.heapLargestInMin = UINT32_MAX;
      g_peakPsramMin[m][a] = g_peakHeapMin[m][a] = 0;
    }
  }
}

// ---------- Máy trạng thái ----------
enum class Phase : uint8_t { BOOT_WAIT, VISITS, SOAK, DONE };
enum class VStep : uint8_t { START, WAIT_ARRIVE, DWELL, WAIT_HOME, SETTLE, FAIL_HOME };
enum class SStep : uint8_t { PICK, ENTER, WAIT_ARRIVE, DWELL, WAIT_HOME, THEME_SET, THEME_WAIT, IDLE, SETTLE, FINISH };

Phase g_phase = Phase::BOOT_WAIT;
uint32_t g_phaseStart = 0;

// visit
int g_mode = 0;       // 0 = không nhạc, 1 = có nhạc
int g_app = 0, g_rep = 0;
VStep g_vstep = VStep::START;
uint32_t g_vStart = 0, g_arrive = 0, g_goStamp = 0;
Mem g_before;
Mem g_in;

// soak
SStep g_sstep = SStep::PICK;
uint32_t g_soakStart = 0, g_sStepStart = 0, g_nextSample = 0, g_dwellTarget = 0;
uint32_t g_rng = 12345u;
int g_curApp = 0;
bool g_wantSample = false;
int g_themeLeft = 0;
int g_actions = 0, g_quickActions = 0, g_themeActions = 0, g_idleActions = 0, g_appActions = 0, g_abort = 0;
int g_ns = 0;
uint32_t g_sPsram[kMaxSoakSamples], g_sHeap[kMaxSoakSamples], g_sLarge[kMaxSoakSamples],
    g_sHeapLarge[kMaxSoakSamples], g_sTasks[kMaxSoakSamples];

uint32_t rnd() {
  g_rng = g_rng * 1664525u + 1013904223u;
  return g_rng >> 8;
}

void startVisitsPhase(uint32_t now);
void startSoakPhase(uint32_t now);

void printVisitSummary(int mode) {
  Serial.printf("PERF,=== TONG KET %s ===\n", mode == 0 ? "KHONG NHAC" : "CO NHAC NEN");
  Serial.println("PERF,TABLE,app,n,fail,to_cold_ms,to_min,to_avg,to_max,back_cold_ms,back_min,back_avg,back_max,"
                 "tick_to_max_ms,tick_back_max_ms,foot_psram_B,foot_heap_B,psram_after_min_B,"
                 "psram_largest_in_min,heap_largest_in_min,psram_peak_min_free,heap_peak_min_free");
  for (int a = 0; a < kNumApps; ++a) {
    const AppStat& s = g_stat[mode][a];
    const int warm = s.n > kWarmReps ? s.n - kWarmReps : 0;  // số lần "ấm" (đã trừ lần nguội)
    const double toAvg = warm > 0 ? (double)s.toSum / warm : 0.0;
    const double backAvg = warm > 0 ? (double)s.backSum / warm : 0.0;
    Serial.printf("PERF,TABLE,%s,%d,%d,%lu,%lu,%.0f,%lu,%lu,%lu,%.0f,%lu,%lu,%lu,%ld,%ld,%ld,%lu,%lu,%lu,%lu\n",
                  kApps[a].name, s.n, s.fail, (unsigned long)s.toCold,
                  (unsigned long)(s.toMin == UINT32_MAX ? 0 : s.toMin), toAvg, (unsigned long)s.toMax,
                  (unsigned long)s.backCold, (unsigned long)(s.backMin == UINT32_MAX ? 0 : s.backMin),
                  backAvg, (unsigned long)s.backMax, (unsigned long)(s.tickToMaxUs / 1000),
                  (unsigned long)(s.tickBackMaxUs / 1000),
                  (long)(s.footPsramMax == INT32_MIN ? 0 : s.footPsramMax),
                  (long)(s.footHeapMax == INT32_MIN ? 0 : s.footHeapMax),
                  (long)(s.afterMin == INT32_MAX ? 0 : s.afterMin),
                  (unsigned long)(s.largestInMin == UINT32_MAX ? 0 : s.largestInMin),
                  (unsigned long)(s.heapLargestInMin == UINT32_MAX ? 0 : s.heapLargestInMin),
                  (unsigned long)g_peakPsramMin[mode][a], (unsigned long)g_peakHeapMin[mode][a]);
  }
  Serial.println("PERF,GHI_CHU,to_/back_ = ms tu luc goi goTo/goHome den luc AppManager bao da chuyen (do phan giai ~5 ms); "
                 "cold = lan dau; to_avg/back_avg khong tinh 2 lan nguoi; foot_* = bo nho app chiem khi dang mo; "
                 "psram_after_min < 0 la hut sau khi thoat; *_peak_min_free = dinh dung tu luc khoi dong");
  if (mode == 1) {
    Serial.printf("PERF,AUDIO,tong_ket_phan2,cua_so=%d,dut=%d,ratio_min=%.2f,loop_gap_max_ms=%lu,phat_lai=%d,play_loi=%d\n",
                  g_mon.windows, g_mon.stalls, g_mon.windows > 0 ? g_mon.minRatio : -1.0,
                  (unsigned long)g_mon.gapMax, g_mon.restarts, g_mon.playFail);
  }
}

// ---------- PHẦN 1 / 2: VISIT ----------
void runVisit(uint32_t now) {
  const AppDef& ad = kApps[g_app];
  AppStat& s = g_stat[g_mode][g_app];
  switch (g_vstep) {
    case VStep::START:
      g_before = takeMem();
      g_goStamp = millis();
      g_tickMaxUs = 0;
      AppManager::goTo(ad.id);
      g_vstep = VStep::WAIT_ARRIVE;
      g_vStart = now;
      break;

    case VStep::WAIT_ARRIVE:
      if (AppManager::current() == ad.id) {
        const uint32_t ms = millis() - g_goStamp;
        ++s.n;
        const bool cold = (g_rep < kWarmReps);
        if (cold) {
          if (g_rep == 0) s.toCold = ms;
        } else {
          if (ms < s.toMin) s.toMin = ms;
          if (ms > s.toMax) s.toMax = ms;
          s.toSum += ms;
        }
        if (g_tickMaxUs > s.tickToMaxUs) s.tickToMaxUs = g_tickMaxUs;
        g_arrive = now;
        g_vstep = VStep::DWELL;
      } else if (now - g_vStart > kSwitchTimeoutMs) {
        ++s.fail;
        Serial.printf("PERF,LOI,khong_chuyen_duoc_sang,%s\n", ad.name);
        g_vstep = VStep::FAIL_HOME;
        AppManager::goHome();
        g_vStart = now;
      }
      break;

    case VStep::DWELL:
      if (now - g_arrive < ad.dwell) break;
      g_in = takeMem();
      sampleTaskStacks();
      {
        const int32_t fp = (int32_t)g_before.psramFree - (int32_t)g_in.psramFree;
        const int32_t fh = (int32_t)g_before.heapFree - (int32_t)g_in.heapFree;
        if (g_rep >= kWarmReps) {  // chỉ tính lần ấm: lần nguội có thể dựng bộ đệm lần đầu
          if (fp > s.footPsramMax) s.footPsramMax = fp;
          if (fp < s.footPsramMin) s.footPsramMin = fp;
          if (fh > s.footHeapMax) s.footHeapMax = fh;
          if (fh < s.footHeapMin) s.footHeapMin = fh;
          if (g_in.psramLargest < s.largestInMin) s.largestInMin = g_in.psramLargest;
          if (g_in.heapLargest < s.heapLargestInMin) s.heapLargestInMin = g_in.heapLargest;
        }
        Serial.printf("PERF,VISIT,%s,mode=%d,rep=%d,psram_before=%lu,psram_in=%lu,heap_before=%lu,heap_in=%lu,"
                      "psram_largest_in=%lu,heap_largest_in=%lu,tasks_in=%lu\n",
                      ad.name, g_mode, g_rep, (unsigned long)g_before.psramFree, (unsigned long)g_in.psramFree,
                      (unsigned long)g_before.heapFree, (unsigned long)g_in.heapFree,
                      (unsigned long)g_in.psramLargest, (unsigned long)g_in.heapLargest, (unsigned long)g_in.tasks);
      }
      g_goStamp = millis();
      g_tickMaxUs = 0;
      AppManager::goHome();
      g_vstep = VStep::WAIT_HOME;
      g_vStart = now;
      break;

    case VStep::WAIT_HOME:
      if (AppManager::current() == AppId::MAIN_SCREEN) {
        const uint32_t ms = millis() - g_goStamp;
        if (g_rep < kWarmReps) {
          if (g_rep == 0) s.backCold = ms;
        } else {
          if (ms < s.backMin) s.backMin = ms;
          if (ms > s.backMax) s.backMax = ms;
          s.backSum += ms;
        }
        if (g_tickMaxUs > s.tickBackMaxUs) s.tickBackMaxUs = g_tickMaxUs;
        g_vstep = VStep::SETTLE;
        g_vStart = now;
      } else if (now - g_vStart > kSwitchTimeoutMs) {
        ++s.fail;
        Serial.printf("PERF,LOI,khong_ve_duoc_Main,%s\n", ad.name);
        AppManager::goHome();
        g_vStart = now;
      }
      break;

    case VStep::FAIL_HOME:
      if (AppManager::current() == AppId::MAIN_SCREEN) {
        g_vstep = VStep::SETTLE;
        g_vStart = now;
      } else if (now - g_vStart > kSwitchTimeoutMs) {
        AppManager::goHome();
        g_vStart = now;
      }
      break;

    case VStep::SETTLE: {
      if (now - g_vStart < kSettleMs) break;
      const Mem after = takeMem();
      const int32_t d = (int32_t)after.psramFree - (int32_t)g_before.psramFree;
      if (g_rep >= kWarmReps && d < s.afterMin) s.afterMin = d;
      Serial.printf("PERF,VISIT,%s,mode=%d,rep=%d,psram_after=%lu,psram_after_minus_before=%ld,tasks_after=%lu\n",
                    ad.name, g_mode, g_rep, (unsigned long)after.psramFree, (long)d, (unsigned long)after.tasks);
      ++g_rep;
      if (g_rep >= kReps) {
        g_peakPsramMin[g_mode][g_app] = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        g_peakHeapMin[g_mode][g_app] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        g_rep = 0;
        ++g_app;
      }
      if (g_app >= kNumApps) {
        printVisitSummary(g_mode);
        printTaskStacks(g_mode == 0 ? "sau_phan1" : "sau_phan2");
        if (g_mode == 0) {
          g_mode = 1;
          g_app = 0;
          if (!startAudio(now)) {
            Serial.println("PERF,LOI,khong_phat_duoc_nhac_(can_it_nhat_1_file_.mp3_trong_/music),bo_qua_phan_2_va_3");
            g_phase = Phase::DONE;
            break;
          }
          g_phaseStart = now;  // chờ ring nạp đầy trước khi đo (xem loop())
          g_rep = 0;
          g_vstep = VStep::START;
        } else {
#ifdef PERF_SKIP_SOAK
          stopAudio();
          g_phase = Phase::DONE;
          Serial.println("PERF,XONG");
#else
          startSoakPhase(now);
#endif
        }
      } else {
        g_vstep = VStep::START;
      }
      break;
    }
  }
}

// ---------- PHẦN 3: SOAK ----------
void soakSample(uint32_t now) {
  if (g_ns >= kMaxSoakSamples) return;
  const Mem m = takeMem();
  g_sPsram[g_ns] = m.psramFree;
  g_sHeap[g_ns] = m.heapFree;
  g_sLarge[g_ns] = m.psramLargest;
  g_sHeapLarge[g_ns] = m.heapLargest;
  g_sTasks[g_ns] = m.tasks;
  sampleTaskStacks();
  Serial.printf("PERF,SOAK,%d,t_s=%lu,heap_free=%lu,heap_largest=%lu,psram_free=%lu,psram_largest=%lu,tasks=%lu,"
                "loop_stack_min=%lu,audio_dut=%d,audio_ratio_min=%.2f,loop_gap_max_ms=%lu,tick_max_ms=%lu,hanh_dong=%d\n",
                g_ns, (unsigned long)((now - g_soakStart) / 1000), (unsigned long)m.heapFree,
                (unsigned long)m.heapLargest, (unsigned long)m.psramFree, (unsigned long)m.psramLargest,
                (unsigned long)m.tasks, (unsigned long)uxTaskGetStackHighWaterMark(nullptr), g_mon.stalls,
                g_mon.windows > 0 ? g_mon.minRatio : -1.0, (unsigned long)g_mon.gapMax,
                (unsigned long)(g_tickMaxAllUs / 1000), g_actions);
  ++g_ns;
}

void printSoakSummary(uint32_t now) {
  Serial.println("PERF,=== TONG KET SOAK ===");
  const int ref = kSoakWarmSamples;
  bool psramOk = true, heapOk = true, tasksOk = true;
  int32_t psramDrift = 0, heapDrift = 0, largeDrift = 0;
  double psramSlope = 0, heapSlope = 0;
  uint32_t psramMin = UINT32_MAX, heapMin = UINT32_MAX, heapLargeMin = UINT32_MAX;
  if (g_ns > ref + 1) {
    const int n = g_ns - ref;
    psramDrift = (int32_t)g_sPsram[g_ns - 1] - (int32_t)g_sPsram[ref];
    heapDrift = (int32_t)g_sHeap[g_ns - 1] - (int32_t)g_sHeap[ref];
    largeDrift = (int32_t)g_sLarge[g_ns - 1] - (int32_t)g_sLarge[ref];
    psramSlope = slopeOf(g_sPsram + ref, n);
    heapSlope = slopeOf(g_sHeap + ref, n);
    psramOk = !(psramDrift < -kTol || psramSlope * (n - 1) < -kTol);
    heapOk = !(heapDrift < -kTol || heapSlope * (n - 1) < -kTol);
    for (int i = ref; i < g_ns; ++i) {
      if (g_sTasks[i] != g_sTasks[ref]) tasksOk = false;
      if (g_sPsram[i] < psramMin) psramMin = g_sPsram[i];
      if (g_sHeap[i] < heapMin) heapMin = g_sHeap[i];
      if (g_sHeapLarge[i] < heapLargeMin) heapLargeMin = g_sHeapLarge[i];
    }
  } else {
    Serial.println("PERF,CANH_BAO,khong_du_mau_soak_de_ket_luan");
  }
  const bool audioOk = g_audioActive && g_mon.playFail == 0 && g_mon.windows >= 5;
  const bool stallOk = g_mon.stalls == 0;
  Serial.printf("PERF,SOAK_KQ,thoi_gian_s=%lu,mau=%d,hanh_dong=%d(app=%d,thoat_nhanh=%d,doi_theme=%d,dung_yen=%d,huy=%d)\n",
                (unsigned long)((now - g_soakStart) / 1000), g_ns, g_actions, g_appActions, g_quickActions,
                g_themeActions, g_idleActions, g_abort);
  Serial.printf("PERF,SOAK_KQ,psram_troi_B=%ld,psram_B_moi_phut=%.1f,heap_troi_B=%ld,heap_B_moi_phut=%.1f,"
                "psram_largest_troi_B=%ld\n",
                (long)psramDrift, psramSlope, (long)heapDrift, heapSlope, (long)largeDrift);
  Serial.printf("PERF,SOAK_KQ,psram_free_min_B=%lu,heap_free_min_B=%lu,heap_largest_min_B=%lu,"
                "dinh_psram_min_free_B=%lu,dinh_heap_min_free_B=%lu\n",
                (unsigned long)(psramMin == UINT32_MAX ? 0 : psramMin),
                (unsigned long)(heapMin == UINT32_MAX ? 0 : heapMin),
                (unsigned long)(heapLargeMin == UINT32_MAX ? 0 : heapLargeMin),
                (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("PERF,SOAK_KQ,audio_cua_so=%d,audio_dut=%d,audio_ratio_min=%.2f,loop_gap_max_ms=%lu,"
                "tick_max_ms=%lu,phat_lai=%d,play_loi=%d,trang_thai_cuoi=%d\n",
                g_mon.windows, g_mon.stalls, g_mon.windows > 0 ? g_mon.minRatio : -1.0,
                (unsigned long)g_mon.gapMax, (unsigned long)(g_tickMaxAllUs / 1000), g_mon.restarts,
                g_mon.playFail, (int)AudioService::state());
  const bool pass = psramOk && heapOk && tasksOk && audioOk && stallOk;
  Serial.printf("PERF,SOAK_KQ,ket_qua=%s,psram=%s,heap=%s,task=%s,audio=%s,dut_tieng=%s\n", pass ? "DAT" : "KHONG DAT",
                psramOk ? "ok" : "HUT", heapOk ? "ok" : "HUT", tasksOk ? "ok" : "DOI", audioOk ? "ok" : "LOI",
                stallOk ? "khong" : "CO");
  printTaskStacks("sau_soak");
}

void startSoakPhase(uint32_t now) {
  Serial.printf("PERF,=== SOAK %d phut: nhac nen + hanh dong ngau nhien, lay mau moi %lu s o Main ===\n",
                (int)PERF_SOAK_MIN, (unsigned long)(kSoakSampleMs / 1000));
  g_phase = Phase::SOAK;
  g_soakStart = now;
  g_nextSample = now + kSoakSampleMs;
  g_sstep = SStep::PICK;
  g_wantSample = false;
  g_ns = 0;
  g_actions = g_quickActions = g_themeActions = g_idleActions = g_appActions = g_abort = 0;
  g_tickMaxAllUs = 0;
  resetMon();
  g_mon.valid = false;
  g_audioStart = now;  // cho ring nạp lại
}

void runSoak(uint32_t now) {
  if (now - g_soakStart >= kSoakMs && g_sstep == SStep::PICK) {
    g_sstep = SStep::FINISH;
    g_sStepStart = now;
  }
  if (now >= g_nextSample) g_wantSample = true;

  switch (g_sstep) {
    case SStep::PICK: {
      if (g_wantSample) {
        g_sstep = SStep::SETTLE;
        g_sStepStart = now;
        break;
      }
      const uint32_t r = rnd() % 100;
      ++g_actions;
      if (r < 55) {  // vào app, ở lại 1-5 s
        g_curApp = (int)(rnd() % kNumApps);
        g_dwellTarget = 1000 + (rnd() % 4001);
        if (kApps[g_curApp].id == AppId::APP_MUSIC_PLAYER && g_dwellTarget < 4000) g_dwellTarget += 3000;
        ++g_appActions;
        g_sstep = SStep::ENTER;
      } else if (r < 70) {  // thoát nhanh
        g_curApp = (int)(rnd() % kNumApps);
        g_dwellTarget = 50 + (rnd() % 400);
        ++g_quickActions;
        g_sstep = SStep::ENTER;
      } else if (r < 85) {  // đổi theme đủ vòng
        LvglLock lock;
        g_themeLeft = (int)Themes::count();
        ++g_themeActions;
        g_sstep = SStep::THEME_SET;
      } else {  // đứng yên ở Main
        g_dwellTarget = 2000 + (rnd() % 4001);
        ++g_idleActions;
        g_sStepStart = now;
        g_sstep = SStep::IDLE;
      }
      break;
    }

    case SStep::ENTER:
      g_tickMaxUs = 0;
      AppManager::goTo(kApps[g_curApp].id);
      g_sStepStart = now;
      g_sstep = SStep::WAIT_ARRIVE;
      break;

    case SStep::WAIT_ARRIVE:
      if (AppManager::current() == kApps[g_curApp].id) {
        g_sStepStart = now;
        g_sstep = SStep::DWELL;
      } else if (now - g_sStepStart > kSwitchTimeoutMs) {
        ++g_abort;
        Serial.printf("PERF,LOI,soak_khong_chuyen_duoc_sang,%s\n", kApps[g_curApp].name);
        AppManager::goHome();
        g_sStepStart = now;
        g_sstep = SStep::WAIT_HOME;
      }
      break;

    case SStep::DWELL:
      if (now - g_sStepStart >= g_dwellTarget) {
        AppManager::goHome();
        g_sStepStart = now;
        g_sstep = SStep::WAIT_HOME;
      }
      break;

    case SStep::WAIT_HOME:
      if (AppManager::current() == AppId::MAIN_SCREEN) {
        // thoát nhanh xong còn task nền dọn: chờ ngắn trước hành động kế
        g_sStepStart = now;
        g_dwellTarget = 600;
        g_sstep = SStep::IDLE;
      } else if (now - g_sStepStart > kSwitchTimeoutMs) {
        ++g_abort;
        Serial.println("PERF,LOI,soak_khong_ve_duoc_Main");
        AppManager::goHome();
        g_sStepStart = now;
      }
      break;

    case SStep::THEME_SET:
      if (g_themeLeft <= 0) {
        g_sstep = SStep::PICK;
        break;
      }
      {
        LvglLock lock;
        const char* cur = Themes::current().key;
        size_t idx = 0;
        for (size_t i = 0; i < Themes::count(); ++i) {
          if (strcmp(Themes::at(i).key, cur) == 0) {
            idx = i;
            break;
          }
        }
        Themes::select(Themes::at((idx + 1) % Themes::count()).key);
        AppManager::reload();
      }
      --g_themeLeft;
      g_sStepStart = now;
      g_sstep = SStep::THEME_WAIT;
      break;

    case SStep::THEME_WAIT:
      if (now - g_sStepStart >= 1200) g_sstep = SStep::THEME_SET;
      break;

    case SStep::IDLE:
      if (now - g_sStepStart >= g_dwellTarget) g_sstep = SStep::PICK;
      break;

    case SStep::SETTLE:
      if (AppManager::current() != AppId::MAIN_SCREEN) {
        AppManager::goHome();
        g_sStepStart = now;
        break;
      }
      if (now - g_sStepStart < kSettleMs) break;
      soakSample(now);
      g_nextSample += kSoakSampleMs;
      if (g_nextSample < now) g_nextSample = now + kSoakSampleMs;
      g_wantSample = false;
      g_sstep = SStep::PICK;
      break;

    case SStep::FINISH:
      if (AppManager::current() != AppId::MAIN_SCREEN) {
        AppManager::goHome();
        g_sStepStart = now;
        break;
      }
      if (now - g_sStepStart < kSettleMs) break;
      soakSample(now);
      printSoakSummary(now);
      stopAudio();
      g_phase = Phase::DONE;
      Serial.println("PERF,XONG");
      break;
  }
}

void startVisitsPhase(uint32_t now) {
  resetStats();
  g_mode = 0;
  g_app = 0;
  g_rep = 0;
  g_vstep = VStep::START;
  g_phase = Phase::VISITS;
  Serial.printf("PERF,=== PHAN 1: %d app x %d lan, khong nhac (2 lan dau la nguoi) ===\n", kNumApps, kReps);
  (void)now;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // Trình tự giống firmware chính (ARCHITECTURE.md mục 5, 6.4, 6.8).
  LvglPort::init();
  {
    LvglLock lock;
    InputService::init();
  }
  SdStorage::begin();
  TimeService::begin();
  SettingsStore::begin();
  {
    LvglLock lock;
    AppManager::init();
  }
  g_phaseStart = millis();
}

void loop() {
  const uint32_t now = millis();
  const uint32_t t0 = micros();
  AppManager::tick(now);
  const uint32_t dur = micros() - t0;
  if (dur > g_tickMaxUs) g_tickMaxUs = dur;
  if (dur > g_tickMaxAllUs) g_tickMaxAllUs = dur;
  audioTick();

  switch (g_phase) {
    case Phase::BOOT_WAIT:
      if (now - g_phaseStart >= kBootWaitMs) {
        Serial.println("PERF,=== BAT DAU DO (KHONG BAM PHIM) ===");
        const Mem m0 = takeMem();
        Serial.printf("PERF,MOC,heap_free=%lu,heap_largest=%lu,psram_free=%lu,psram_largest=%lu,tasks=%lu\n",
                      (unsigned long)m0.heapFree, (unsigned long)m0.heapLargest, (unsigned long)m0.psramFree,
                      (unsigned long)m0.psramLargest, (unsigned long)m0.tasks);
#ifdef PERF_SKIP_VISITS
        if (!startAudio(now)) {
          Serial.println("PERF,LOI,khong_phat_duoc_nhac");
          g_phase = Phase::DONE;
        } else {
          startSoakPhase(now);
        }
#else
        startVisitsPhase(now);
#endif
      }
      break;

    case Phase::VISITS:
      // Sau khi bật nhạc ở phần 2, chờ kAudioPrimeMs cho ring nạp đầy rồi mới vào app đầu tiên.
      if (g_mode == 1 && g_app == 0 && g_rep == 0 && g_vstep == VStep::START && now - g_phaseStart < 3000) break;
      runVisit(now);
      break;

    case Phase::SOAK:
      runSoak(now);
      break;

    case Phase::DONE:
      break;
  }

  vTaskDelay(pdMS_TO_TICKS(5));
}
