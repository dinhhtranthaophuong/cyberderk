#pragma once
// Diem dieu khien / quan sat cua stub (chi dung trong test).
#include <stdint.h>
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace hooks {
// dong ho ao
extern std::atomic<uint32_t> nowMs;
// bo nho (LVGL + PSRAM deu qua heap_caps)
extern std::atomic<long> heapBytes;     // byte dang giu
extern std::atomic<long> heapBlocks;    // so khoi dang giu
extern std::atomic<long> heapAllocs;    // tong so lan cap phat
extern std::atomic<long> failAfter;     // >=0: sau N lan cap phat nua thi moi lan cap phat deu that bai
extern std::atomic<long> failCount;
extern std::atomic<size_t> failMinSize;  // chi cap phat >= cỡ này mới bị tiêm lỗi (LVGL het bo nho thi tu treo)
// task
extern std::atomic<int> liveTasks;
extern std::atomic<int> tasksCreated;
extern std::atomic<int> taskCreateFail;
// khoa
extern std::atomic<int> lockViolations;  // khoa/nha khong dung chu
// tren man hinh
extern std::atomic<long> flushCount, flushBadArea;
// SD gia lap (trong bo nho)
extern std::atomic<int> sdMounted;
extern std::atomic<int> sdWriteFail;
extern std::atomic<int> sdReadDelayMs;
extern std::atomic<long> sdWrites;
void fsClear();
void fsPut(const std::string& path, const std::vector<uint8_t>& data);
void fsPutStr(const std::string& path, const std::string& s);
bool fsGet(const std::string& path, std::vector<uint8_t>* out);
// JPEG gia
void putJpg(const char* path, int W, int H, int seed, int mode = 0);
extern std::atomic<int> decodeDelayMs;   // moi lan giai ma cham them (de job ton tai khi roi app)
extern std::atomic<long> decodeCalls;
// thoi gian (DateTime tra ve boi TimeService::now())
extern std::atomic<int> tYear, tMonth, tDay, tHour, tMin;
// task dang chay cua Album: de test
}
