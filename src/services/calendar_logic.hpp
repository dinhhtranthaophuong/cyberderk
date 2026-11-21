#pragma once
// CalendarLogic: phan thuan C++ cua app Calendar (khong LVGL, khong Arduino).
// Tach rieng de bien dich va test duoc bang g++ thuong tren may tinh (tools/calendar_test).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CalendarLogic {

struct Event {
    uint16_t year;
    uint8_t month, day;
    std::string title;  // ASCII, toi da kMaxTitle ky tu
};

constexpr int kMinYear = 2000;  // khop pham vi DS3231 cua TimeService
constexpr int kMaxYear = 2099;
constexpr size_t kMaxTitle = 48;

bool isLeap(int y);
int daysInMonth(int y, int m);
int weekdayOf(int y, int m, int d);  // 0 = Chu Nhat

// Cong/tru ngay. Neu ket qua ra ngoai [kMinYear, kMaxYear] thi giu nguyen, tra false.
bool addDays(int& y, int& m, int& d, int delta);
// Doi thang, ngay bi kep vao so ngay cua thang moi. Ngoai pham vi nam thi giu nguyen, tra false.
bool shiftMonth(int& y, int& m, int& d, int delta);

// Doc su kien tu noi dung JSON (khong can ket thuc bang '\0'). Dang file:
//   {"events":[{"date":"2026-11-20","title":"Nop bai"}, ...]}
// (mang tran cung duoc). Bo qua muc sai ngay/thieu title. Tra so su kien da them (toi da maxEvents).
size_t parseEvents(const char* text, size_t len, std::vector<Event>& out, size_t maxEvents);

}  // namespace CalendarLogic
