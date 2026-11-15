#pragma once
// Diem dieu khien/quan sat cua cac stub (chi dung trong test may tinh).
#include <stdint.h>
#include <atomic>
#include <string>
#include <vector>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"

namespace hooks {
// ---- thoi gian mo phong ----
extern double timeScale;          // >1: delay/i2s chay nhanh hon thoi gian that
// ---- kiem tra khoa ----
extern std::atomic<int> lockOrderViolations;
extern std::atomic<int> relockErrors;
std::string firstLockMessage();
// ---- task ----
struct TaskRec { std::string name; uint32_t stack; int prio; int core; };
std::vector<TaskRec> tasks();
// ---- PSRAM ----
extern std::atomic<long> psramOutstanding;   // byte dang giu
extern std::atomic<long> psramAllocs;
extern std::atomic<int> psramFailNth;        // >0: lan cap phat thu N that bai (dem tu luc dat)
void psramResetCounter();
// ---- I2S ----
extern std::atomic<int> i2sInstallFail;      // 1 = install tra loi
extern std::atomic<int> i2sInstallCalls;
extern std::atomic<int> i2sZeroCalls;
extern i2s_config_t i2sCfg;
extern i2s_pin_config_t i2sPins;
std::vector<uint32_t> i2sRateChanges();
void captureClear();
size_t captureFrames();                       // so frame stereo da ghi
std::vector<int16_t> captureLeft(size_t from = 0);
extern std::atomic<long> captureLRMismatch;   // so frame L != R
// ---- SD ----
void sdSetRoot(const std::string& root);
extern std::atomic<int> sdMounted;
extern std::atomic<long> sdFailReadAfter;     // >=0: sau N byte doc (tu luc mo file) thi read tra 0
extern std::atomic<int> sdReadDelayUs;
extern std::atomic<int> sdOpenFail;
}  // namespace hooks
