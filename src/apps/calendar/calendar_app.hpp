#pragma once
// Calendar app (APP_CALENDAR). Chi viet trong apps/calendar/.
// Phan logic thuan C++ (ngay/thang/parser JSON) nam o services/calendar_logic.hpp,
// de test duoc bang g++ thuong tren may tinh (tools/calendar_test) ma khong keo theo LVGL.
#include <lvgl.h>
#include <cstdint>
#include <vector>

#include "core/app_base.hpp"
#include "services/calendar_logic.hpp"

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
