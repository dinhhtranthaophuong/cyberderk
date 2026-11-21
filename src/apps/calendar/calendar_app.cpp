#include "apps/calendar/calendar_app.hpp"

#include <cstdio>

#include "config/display.hpp"
#include "config/keymap.hpp"
#include "core/app_manager.hpp"
#include "core/psram.hpp"
#include "core/theme.hpp"
#include "services/input_service.hpp"
#include "services/sd_storage.hpp"
#include "services/time_service.hpp"

namespace {
const char* const kMonthNames[12] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};
const char* const kWeekdayLetters[7] = {"S", "M", "T", "W", "T", "F", "S"};
constexpr const char* kEventsPath = "/data/events.json";
constexpr size_t kMaxFileBytes = 64 * 1024;  // file lon hon thi chi doc 64 KB dau
}  // namespace

using namespace CalendarLogic;

void CalendarApp::readToday() {
    DateTime now = TimeService::now();
    todayValid_ = (now.year >= kMinYear && now.year <= kMaxYear);
    if (todayValid_) {
        todayY_ = now.year; todayM_ = now.month; todayD_ = now.day;
    } else {
        todayY_ = 0; todayM_ = 0; todayD_ = 0;
    }
}

void CalendarApp::loadEvents() {
    events_.clear();
    if (!SdStorage::isMounted()) return;
    size_t size = 0;
    void* data = SdStorage::readFileToPsram(kEventsPath, &size);
    if (!data) return;  // chua co file: lich van chay, khong co cham su kien
    if (size > kMaxFileBytes) size = kMaxFileBytes;
    parseEvents(static_cast<const char*>(data), size, events_, kMaxEvents);
    Psram::release(data);
    printf("[Calendar] %u events\n", static_cast<unsigned>(events_.size()));
}

void CalendarApp::onCreate(lv_obj_t* parent) {
    const Theme& th = Themes::current();

    readToday();
    if (todayValid_) {
        selY_ = todayY_; selM_ = todayM_; selD_ = todayD_;
    } else {
        selY_ = kMinYear; selM_ = 1; selD_ = 1;
    }
    evIdx_ = 0;
    lastTickMs_ = 0;
    loadEvents();

    // Bo cuc: tieu de / hang thu / luoi 6 hang / chan man (su kien cua ngay chon)
    const int headerH = UI_COMPACT ? 16 : 28;
    const int wdH = UI_COMPACT ? 12 : 18;
    const int footerH = UI_COMPACT ? 14 : 24;
    const int gridH = UI_H - headerH - wdH - footerH;
    const int rowH = gridH / 6;
    const int cellW = UI_W / 7;
    const int gx = (UI_W - cellW * 7) / 2;
    const int gy = headerH + wdH;

    // Widget chinh: nhan focus + phim mui ten/ENTER/ESC
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, UI_W, UI_H);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_outline_width(root_, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(root_, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);

    // Hai widget gac 1x1: LVGL 8.3 dung phim 7/9 de doi focus, nen focus roi sang gac = nguoi dung bam 7/9.
    guardPrev_ = lv_obj_create(parent);
    guardNext_ = lv_obj_create(parent);
    for (lv_obj_t* g : {guardPrev_, guardNext_}) {
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, 1, 1);
        lv_obj_set_pos(g, 0, 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_outline_width(g, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(g, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    }

    title_ = lv_label_create(root_);
    lv_obj_set_style_text_font(title_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_, lv_color_hex(th.text), 0);
    lv_obj_set_width(title_, UI_W);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title_, 0, (headerH - 14) / 2);

    for (int c = 0; c < 7; ++c) {
        wd_[c] = lv_label_create(root_);
        lv_label_set_text(wd_[c], kWeekdayLetters[c]);
        lv_obj_set_style_text_color(wd_[c], lv_color_hex(th.accent), 0);
        lv_obj_set_width(wd_[c], cellW);
        lv_obj_set_style_text_align(wd_[c], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(wd_[c], gx + c * cellW, headerH + 1);
    }

    for (int i = 0; i < kCells; ++i) {
        const int row = i / 7, col = i % 7;
        lv_obj_t* cell = lv_obj_create(root_);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, gx + col * cellW + 1, gy + row * rowH + 1);
        lv_obj_set_size(cell, cellW - 2, rowH - 2);
        lv_obj_set_style_radius(cell, 3, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(cell);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -1);

        lv_obj_t* dot = lv_obj_create(cell);
        lv_obj_remove_style_all(dot);
        const int ds = UI_COMPACT ? 3 : 4;
        lv_obj_set_size(dot, ds, ds);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -1);

        cell_[i] = cell;
        lbl_[i] = lbl;
        dot_[i] = dot;
    }

    footer_ = lv_label_create(root_);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(footer_, UI_W - 8);
    lv_obj_set_style_text_color(footer_, lv_color_hex(th.text), 0);
    lv_obj_set_pos(footer_, 4, UI_H - footerH + (footerH - 12) / 2);

    // Thu tu group: gac trai, chinh, gac phai (quay vong) -> 7 = PREV tro vao gac trai, 9 = NEXT tro vao gac phai.
    lv_group_t* grp = InputService::defaultGroup();
    lv_group_add_obj(grp, guardPrev_);
    lv_group_add_obj(grp, root_);
    lv_group_add_obj(grp, guardNext_);
    lv_obj_add_event_cb(root_, onRootEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(guardPrev_, onGuardPrev, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(guardNext_, onGuardNext, LV_EVENT_FOCUSED, this);
    lv_group_focus_obj(root_);

    refresh();
}

void CalendarApp::onDestroy() {
    // Callback co the chay sau day (LVGL don group/huy screen): dat nullptr de callback bo qua.
    root_ = nullptr;
    guardPrev_ = nullptr;
    guardNext_ = nullptr;
    title_ = nullptr;
    footer_ = nullptr;
    for (int c = 0; c < 7; ++c) wd_[c] = nullptr;
    for (int i = 0; i < kCells; ++i) { cell_[i] = nullptr; lbl_[i] = nullptr; dot_[i] = nullptr; }
    events_.clear();
    events_.shrink_to_fit();
}

bool CalendarApp::onKey(uint32_t key) {
    if (!root_) return false;
    if (key == KEY_OPTION) {  // phim 3: ve hom nay
        goToday();
        return true;
    }
    return false;
}

void CalendarApp::onTick(uint32_t nowMs) {
    if (!root_) return;
    if (lastTickMs_ != 0 && nowMs - lastTickMs_ < 1000) return;
    lastTickMs_ = nowMs ? nowMs : 1;
    const int oy = todayY_, om = todayM_, od = todayD_;
    const bool ov = todayValid_;
    readToday();
    if (ov != todayValid_ || oy != todayY_ || om != todayM_ || od != todayD_) refresh();  // qua nua dem: doi ngay "hom nay"
}

// ---- callback widget ----
void CalendarApp::onRootEvent(lv_event_t* e) {
    auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(e));
    if (!self || !self->root_) return;
    self->handleKey(lv_event_get_key(e));
}

void CalendarApp::onGuardPrev(lv_event_t* e) {
    auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(e));
    if (!self || !self->root_) return;
    lv_group_focus_obj(self->root_);  // tra focus ve widget chinh ngay
    self->stepMonth(-1);
}

void CalendarApp::onGuardNext(lv_event_t* e) {
    auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(e));
    if (!self || !self->root_) return;
    lv_group_focus_obj(self->root_);
    self->stepMonth(+1);
}

// ---- dieu khien ----
void CalendarApp::handleKey(uint32_t key) {
    switch (key) {
        case LV_KEY_LEFT:  moveDays(-1); break;
        case LV_KEY_RIGHT: moveDays(+1); break;
        case LV_KEY_UP:    moveDays(-7); break;
        case LV_KEY_DOWN:  moveDays(+7); break;
        case LV_KEY_ENTER:  // ngay co nhieu su kien: xem su kien ke tiep
            ++evIdx_;
            updateFooter();
            break;
        case LV_KEY_ESC: AppManager::goHome(); break;
        default: break;
    }
}

void CalendarApp::moveDays(int delta) {
    if (!root_) return;
    if (addDays(selY_, selM_, selD_, delta)) {
        evIdx_ = 0;
        refresh();
    }
}

void CalendarApp::stepMonth(int delta) {
    if (!root_) return;
    if (shiftMonth(selY_, selM_, selD_, delta)) {
        evIdx_ = 0;
        refresh();
    }
}

void CalendarApp::goToday() {
    if (!root_) return;
    readToday();
    if (!todayValid_) return;
    selY_ = todayY_; selM_ = todayM_; selD_ = todayD_;
    evIdx_ = 0;
    refresh();
}

// ---- ve ----
void CalendarApp::refresh() {
    if (!root_) return;
    const Theme& th = Themes::current();
    char buf[40];

    snprintf(buf, sizeof(buf), "%s %d", kMonthNames[selM_ - 1], selY_);
    lv_label_set_text(title_, buf);

    const int first = weekdayOf(selY_, selM_, 1);
    const int dim = daysInMonth(selY_, selM_);
    bool hasEv[32] = {};
    for (const Event& ev : events_) {
        if (ev.year == selY_ && ev.month == selM_) hasEv[ev.day] = true;
    }

    for (int i = 0; i < kCells; ++i) {
        const int day = i - first + 1;
        lv_obj_t* c = cell_[i];
        if (!c) continue;
        if (day < 1 || day > dim) {
            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), "%d", day);
        lv_label_set_text(lbl_[i], buf);

        const bool sel = (day == selD_);
        const bool today = todayValid_ && selY_ == todayY_ && selM_ == todayM_ && day == todayD_;
        const uint32_t txt = sel ? th.textOnPanel : th.text;

        lv_obj_set_style_bg_color(c, lv_color_hex(th.panel), 0);
        lv_obj_set_style_bg_opa(c, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (sel) {
            lv_obj_set_style_border_width(c, 2, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(th.focus), 0);
        } else if (today) {
            lv_obj_set_style_border_width(c, 1, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(th.accent), 0);
        } else {
            lv_obj_set_style_border_width(c, 0, 0);
        }
        lv_obj_set_style_text_color(lbl_[i], lv_color_hex(today && !sel ? th.accent : txt), 0);

        if (hasEv[day]) {
            lv_obj_set_style_bg_color(dot_[i], lv_color_hex(sel ? th.textOnPanel : th.accent), 0);
            lv_obj_clear_flag(dot_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(dot_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    updateFooter();
}

void CalendarApp::updateFooter() {
    if (!root_ || !footer_) return;
    char buf[96];
    size_t n = 0;
    for (const Event& ev : events_) {
        if (ev.year == selY_ && ev.month == selM_ && ev.day == selD_) ++n;
    }
    const char* mon = kMonthNames[selM_ - 1];
    if (n == 0) {
        snprintf(buf, sizeof(buf), "%.3s %d: -", mon, selD_);
    } else {
        const size_t want = evIdx_ % n;
        size_t k = 0;
        for (const Event& ev : events_) {
            if (ev.year != selY_ || ev.month != selM_ || ev.day != selD_) continue;
            if (k++ == want) {
                if (n > 1) snprintf(buf, sizeof(buf), "%.3s %d (%u/%u): %s", mon, selD_,
                                    static_cast<unsigned>(want + 1), static_cast<unsigned>(n), ev.title.c_str());
                else snprintf(buf, sizeof(buf), "%.3s %d: %s", mon, selD_, ev.title.c_str());
                break;
            }
        }
    }
    lv_label_set_text(footer_, buf);
}

IApp* createCalendarApp() { return new CalendarApp(); }
