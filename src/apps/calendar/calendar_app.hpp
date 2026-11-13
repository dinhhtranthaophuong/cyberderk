#pragma once
// Calendar app (APP_CALENDAR). Chi viet trong apps/calendar/.
// Phan CalendarLogic la thuan C++ (khong LVGL) nen test duoc tren may tinh.
#include <lvgl.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/app_base.hpp"

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

class CalendarApp : public IApp {
public:
    AppId id() const override { return AppId::APP_CALENDAR; }
    void onCreate(lv_obj_t* parent) override;
    void onDestroy() override;
    bool onKey(uint32_t key) override;
    void onTick(uint32_t nowMs) override;

private:
    static constexpr int kCells = 42;  // 7 cot x 6 hang
    static constexpr size_t kMaxEvents = 256;

    // Callback widget: chay duoc ca sau onDestroy() (kiem tra root_ == nullptr).
    static void onRootEvent(lv_event_t* e);
    static void onGuardPrev(lv_event_t* e);
    static void onGuardNext(lv_event_t* e);

    void handleKey(uint32_t key);
    void moveDays(int delta);
    void stepMonth(int delta);
    void goToday();
    void readToday();
    void loadEvents();
    void refresh();
    void updateFooter();

    lv_obj_t* root_ = nullptr;        // widget chinh, giu focus
    lv_obj_t* guardPrev_ = nullptr;   // widget "gac" cho phim 7
    lv_obj_t* guardNext_ = nullptr;   // widget "gac" cho phim 9
    lv_obj_t* title_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* wd_[7] = {};
    lv_obj_t* cell_[kCells] = {};
    lv_obj_t* lbl_[kCells] = {};
    lv_obj_t* dot_[kCells] = {};

    int selY_ = 2000, selM_ = 1, selD_ = 1;           // ngay dang chon (thang dang xem = thang cua no)
    int todayY_ = 0, todayM_ = 0, todayD_ = 0;
    bool todayValid_ = false;
    size_t evIdx_ = 0;                                // su kien dang hien o chan man (khi ngay co nhieu)
    uint32_t lastTickMs_ = 0;
    std::vector<CalendarLogic::Event> events_;
};