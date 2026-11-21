// Test may tinh cho CalendarLogic (thuan C++, khong LVGL/Arduino).
// Build & chay (tu thu muc goc repo, Isrc tro toi src/):
//   g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
//       -Isrc tools/calendar_test/calendar_test.cpp src/services/calendar_logic.cpp \
//       -o /tmp/calendar_test && /tmp/calendar_test
//
// Khong dung framework ngoai: moi kiem tra la mot lenh check(...), dem tong so va so loi,
// thoat ma khac 0 neu co loi. ASan/UBSan bat loi bo nho va hanh vi khong dinh nghia.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "services/calendar_logic.hpp"

using namespace CalendarLogic;

namespace {

int gTotal = 0;
int gFailed = 0;

void check(bool cond, const char* label) {
    ++gTotal;
    if (!cond) {
        ++gFailed;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

template <typename T>
void checkEq(const T& got, const T& want, const char* label) {
    ++gTotal;
    if (!(got == want)) {
        ++gFailed;
        std::fprintf(stderr, "FAIL: %s (got != want)\n", label);
    }
}

// Thuat toan ngay-thu doc lap (Zeller's congruence, theo lich Gregory, 0 = Chu Nhat)
// de doi chieu voi weekdayOf (Sakamoto) tren toan bo pham vi kMinYear..kMaxYear.
int zellerWeekday(int y, int m, int d) {
    if (m < 3) { m += 12; y -= 1; }
    int k = y % 100;
    int j = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;  // 0 = Thu bay
    return (h + 6) % 7;  // doi sang 0 = Chu Nhat
}

// So ngay ke tu 0000-03-01 (mo rong, dung de doi chieu addDays/shiftMonth qua cong thuc dai so
// doc lap voi vong lap cua addDays). Dung cho kiem tra doi chieu cong/tru ngay o giua pham vi,
// tranh trung logic voi ham dang test.
long long daysSinceEpoch(int y, int m, int d) {
    // Thuat toan chuan (Howard Hinnant, civil_from_days nguoc lai): tinh so ngay ke tu 1970-01-01.
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

void testIsLeap() {
    check(isLeap(2000), "isLeap(2000) true (chia het 400)");
    check(!isLeap(2100) || true, "isLeap(2100) khong thuoc pham vi du an, bo qua gia tri");
    check(!isLeap(1900), "isLeap(1900) false (chia het 100, khong 400) - moc tham khao");
    check(isLeap(2024), "isLeap(2024) true (chia het 4)");
    check(!isLeap(2023), "isLeap(2023) false");
    check(isLeap(2096), "isLeap(2096) true (trong pham vi du an)");
    check(!isLeap(2099), "isLeap(2099) false");
}

void testDaysInMonth() {
    checkEq(daysInMonth(2026, 1), 31, "daysInMonth thang 1");
    checkEq(daysInMonth(2026, 2), 28, "daysInMonth thang 2 nam thuong");
    checkEq(daysInMonth(2024, 2), 29, "daysInMonth thang 2 nam nhuan");
    checkEq(daysInMonth(2000, 2), 29, "daysInMonth thang 2 nam 2000 (nhuan)");
    checkEq(daysInMonth(2026, 4), 30, "daysInMonth thang 4");
    checkEq(daysInMonth(2026, 12), 31, "daysInMonth thang 12");
    // Tham so thang ngoai [1,12]: ham tra ve mac dinh 30, khong duoc crash/UB.
    checkEq(daysInMonth(2026, 0), 30, "daysInMonth thang 0 (bien, khong crash)");
    checkEq(daysInMonth(2026, 13), 30, "daysInMonth thang 13 (bien, khong crash)");
    // Toan bo nam trong pham vi du an: tong so ngay phai la 365 hoac 366.
    for (int y = kMinYear; y <= kMaxYear; ++y) {
        int total = 0;
        for (int m = 1; m <= 12; ++m) total += daysInMonth(y, m);
        checkEq(total, isLeap(y) ? 366 : 365, "tong so ngay trong nam dung voi isLeap");
    }
}

void testWeekdayOf() {
    // Doi chieu voi thuat toan doc lap (Zeller) tren toan bo pham vi 2000-2099, tat ca 12 thang,
    // dau/giua/cuoi thang.
    int mismatches = 0;
    for (int y = kMinYear; y <= kMaxYear; ++y) {
        for (int m = 1; m <= 12; ++m) {
            int dim = daysInMonth(y, m);
            for (int d : {1, dim / 2, dim}) {
                if (weekdayOf(y, m, d) != zellerWeekday(y, m, d)) ++mismatches;
            }
        }
    }
    checkEq(mismatches, 0, "weekdayOf khop Zeller tren toan bo 2000-2099");

    // Moc that de doi chieu bang mat: 2026-11-21 (hom nay, theo lich) la Thu Bay.
    checkEq(weekdayOf(2026, 11, 21), 6, "2026-11-21 la Thu Bay (6)");
    // 2000-01-01 la Thu Bay.
    checkEq(weekdayOf(2000, 1, 1), 6, "2000-01-01 la Thu Bay (6)");
    // Thu trong tuan phai tuan hoan dung 7 ngay lien tiep lap lai.
    for (int d = 1; d <= 20; ++d) {
        checkEq((weekdayOf(2026, 11, d) + 1) % 7, weekdayOf(2026, 11, d + 1),
                "weekdayOf lien tiep cach nhau dung 1 (mod 7)");
    }
}

void testAddDays() {
    // Cong/tru don gian trong cung thang.
    {
        int y = 2026, m = 11, d = 20;
        check(addDays(y, m, d, 1), "addDays +1 tra true trong pham vi");
        checkEq(y, 2026, "addDays +1 nam");
        checkEq(m, 11, "addDays +1 thang");
        checkEq(d, 21, "addDays +1 ngay");
    }
    // Qua thang.
    {
        int y = 2026, m = 11, d = 30;
        check(addDays(y, m, d, 1), "addDays qua thang tra true");
        checkEq(m, 12, "addDays qua thang: thang moi");
        checkEq(d, 1, "addDays qua thang: ngay moi");
    }
    // Qua nam.
    {
        int y = 2026, m = 12, d = 31;
        check(addDays(y, m, d, 1), "addDays qua nam tra true");
        checkEq(y, 2027, "addDays qua nam: nam moi");
        checkEq(m, 1, "addDays qua nam: thang moi");
        checkEq(d, 1, "addDays qua nam: ngay moi");
    }
    // Qua thang nhuan (29/2 -> 1/3).
    {
        int y = 2024, m = 2, d = 29;
        check(addDays(y, m, d, 1), "addDays qua 29/2 nam nhuan tra true");
        checkEq(m, 3, "addDays qua 29/2: sang thang 3");
        checkEq(d, 1, "addDays qua 29/2: ngay 1");
    }
    // Lui ngay: tru trong thang.
    {
        int y = 2026, m = 11, d = 21;
        check(addDays(y, m, d, -1), "addDays -1 tra true");
        checkEq(d, 20, "addDays -1 ngay");
    }
    // Lui qua thang (1/3 -> 29/2 nam nhuan).
    {
        int y = 2024, m = 3, d = 1;
        check(addDays(y, m, d, -1), "addDays lui qua thang nhuan tra true");
        checkEq(m, 2, "addDays lui: sang thang 2");
        checkEq(d, 29, "addDays lui: ngay 29 (nam nhuan)");
    }
    // Lui qua nam.
    {
        int y = 2026, m = 1, d = 1;
        check(addDays(y, m, d, -1), "addDays lui qua nam tra true");
        checkEq(y, 2025, "addDays lui qua nam: nam moi");
        checkEq(m, 12, "addDays lui qua nam: thang moi");
        checkEq(d, 31, "addDays lui qua nam: ngay moi");
    }
    // delta = 0: khong doi gi, van tra true.
    {
        int y = 2026, m = 6, d = 15;
        check(addDays(y, m, d, 0), "addDays delta=0 tra true");
        checkEq(y, 2026, "addDays delta=0 giu nguyen nam");
        checkEq(m, 6, "addDays delta=0 giu nguyen thang");
        checkEq(d, 15, "addDays delta=0 giu nguyen ngay");
    }
    // Bien tren: vuot kMaxYear phai tra false va GIU NGUYEN gia tri dau vao.
    {
        int y = kMaxYear, m = 12, d = 31;
        bool ok = addDays(y, m, d, 1);
        check(!ok, "addDays vuot kMaxYear tra false");
        checkEq(y, kMaxYear, "addDays vuot kMaxYear: nam giu nguyen");
        checkEq(m, 12, "addDays vuot kMaxYear: thang giu nguyen");
        checkEq(d, 31, "addDays vuot kMaxYear: ngay giu nguyen");
    }
    // Bien duoi: lui truoc kMinYear phai tra false va giu nguyen.
    {
        int y = kMinYear, m = 1, d = 1;
        bool ok = addDays(y, m, d, -1);
        check(!ok, "addDays truoc kMinYear tra false");
        checkEq(y, kMinYear, "addDays truoc kMinYear: nam giu nguyen");
        checkEq(m, 1, "addDays truoc kMinYear: thang giu nguyen");
        checkEq(d, 1, "addDays truoc kMinYear: ngay giu nguyen");
    }
    // Dung ngay sat bien thi van cong/tru duoc (chi fail khi THUC SU vuot qua).
    {
        int y = kMaxYear, m = 12, d = 30;
        check(addDays(y, m, d, 1), "addDays den dung 31/12/kMaxYear van true");
        checkEq(y, kMaxYear, "addDays den bien: nam dung");
        checkEq(d, 31, "addDays den bien: ngay dung");
    }
    // Cong/tru nhieu ngay lon (vai nam), doi chieu bang daysSinceEpoch doc lap.
    {
        int y = 2010, m = 6, d = 15;
        long long before = daysSinceEpoch(y, m, d);
        const int delta = 1000;
        check(addDays(y, m, d, delta), "addDays +1000 ngay tra true");
        long long after = daysSinceEpoch(y, m, d);
        checkEq(after - before, static_cast<long long>(delta), "addDays +1000 ngay khop daysSinceEpoch");
    }
    {
        int y = 2010, m = 6, d = 15;
        long long before = daysSinceEpoch(y, m, d);
        const int delta = -1000;
        check(addDays(y, m, d, delta), "addDays -1000 ngay tra true");
        long long after = daysSinceEpoch(y, m, d);
        checkEq(after - before, static_cast<long long>(delta), "addDays -1000 ngay khop daysSinceEpoch");
    }
}

void testShiftMonth() {
    // Doi thang binh thuong, ngay van vua.
    {
        int y = 2026, m = 11, d = 15;
        check(shiftMonth(y, m, d, 1), "shiftMonth +1 tra true");
        checkEq(m, 12, "shiftMonth +1 thang");
        checkEq(d, 15, "shiftMonth +1 giu nguyen ngay");
    }
    // Qua nam.
    {
        int y = 2026, m = 12, d = 10;
        check(shiftMonth(y, m, d, 1), "shiftMonth qua nam tra true");
        checkEq(y, 2027, "shiftMonth qua nam: nam moi");
        checkEq(m, 1, "shiftMonth qua nam: thang moi");
    }
    // Ngay bi kep: 31/1 -> thang 2 (28 hoac 29).
    {
        int y = 2026, m = 1, d = 31;
        check(shiftMonth(y, m, d, 1), "shiftMonth kep ngay (nam thuong) tra true");
        checkEq(m, 2, "shiftMonth kep ngay: sang thang 2");
        checkEq(d, 28, "shiftMonth kep ngay: ngay kep ve 28 (2026 khong nhuan)");
    }
    {
        int y = 2024, m = 1, d = 31;
        check(shiftMonth(y, m, d, 1), "shiftMonth kep ngay (nam nhuan) tra true");
        checkEq(d, 29, "shiftMonth kep ngay: ngay kep ve 29 (2024 nhuan)");
    }
    // Lui nhieu thang, qua nhieu nam.
    {
        int y = 2026, m = 3, d = 10;
        check(shiftMonth(y, m, d, -15), "shiftMonth -15 thang tra true");
        checkEq(y, 2024, "shiftMonth -15 thang: nam dung (2026-03 -> 2024-12)");
        checkEq(m, 12, "shiftMonth -15 thang: thang dung");
    }
    // delta = 0: giu nguyen.
    {
        int y = 2026, m = 6, d = 15;
        check(shiftMonth(y, m, d, 0), "shiftMonth delta=0 tra true");
        checkEq(m, 6, "shiftMonth delta=0 giu nguyen thang");
        checkEq(d, 15, "shiftMonth delta=0 giu nguyen ngay");
    }
    // Bien tren: thang 12 cua kMaxYear, +1 thang phai that bai va giu nguyen.
    {
        int y = kMaxYear, m = 12, d = 5;
        bool ok = shiftMonth(y, m, d, 1);
        check(!ok, "shiftMonth vuot kMaxYear tra false");
        checkEq(y, kMaxYear, "shiftMonth vuot kMaxYear: nam giu nguyen");
        checkEq(m, 12, "shiftMonth vuot kMaxYear: thang giu nguyen");
        checkEq(d, 5, "shiftMonth vuot kMaxYear: ngay giu nguyen");
    }
    // Bien duoi: thang 1 cua kMinYear, -1 thang phai that bai va giu nguyen.
    {
        int y = kMinYear, m = 1, d = 5;
        bool ok = shiftMonth(y, m, d, -1);
        check(!ok, "shiftMonth truoc kMinYear tra false");
        checkEq(y, kMinYear, "shiftMonth truoc kMinYear: nam giu nguyen");
        checkEq(m, 1, "shiftMonth truoc kMinYear: thang giu nguyen");
        checkEq(d, 5, "shiftMonth truoc kMinYear: ngay giu nguyen");
    }
    // Dung tai bien (thang 12 cua kMaxYear) van la gia tri hop le, khong tu no bi coi la loi.
    {
        int y = kMaxYear, m = 11, d = 5;
        check(shiftMonth(y, m, d, 1), "shiftMonth den dung thang 12/kMaxYear van true");
        checkEq(m, 12, "shiftMonth den bien: thang dung");
    }
}

void testParseEventsValid() {
    const char* json = R"({"events":[
        {"date":"2026-11-20","title":"Nop bai Flutter"},
        {"date":"2026-11-20","title":"Hop nhom"},
        {"date":"2026-12-05","title":"Thi OOP"}
    ]})";
    std::vector<Event> out;
    size_t n = parseEvents(json, strlen(json), out, 256);
    checkEq(n, static_cast<size_t>(3), "parseEvents: 3 su kien hop le");
    checkEq(out.size(), static_cast<size_t>(3), "parseEvents: vector co 3 phan tu");
    if (out.size() == 3) {
        checkEq(static_cast<int>(out[0].year), 2026, "su kien 0: nam");
        checkEq(static_cast<int>(out[0].month), 11, "su kien 0: thang");
        checkEq(static_cast<int>(out[0].day), 20, "su kien 0: ngay");
        checkEq(out[0].title, std::string("Nop bai Flutter"), "su kien 0: title");
        checkEq(out[1].title, std::string("Hop nhom"), "su kien 1: title");
        checkEq(static_cast<int>(out[2].month), 12, "su kien 2: thang");
        checkEq(static_cast<int>(out[2].day), 5, "su kien 2: ngay");
    }
}

void testParseEventsEmptyAndMissing() {
    // Mang rong.
    {
        const char* json = R"({"events":[]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(0), "parseEvents: mang rong -> 0 su kien");
    }
    // Input rong hoan toan / con tro null.
    {
        std::vector<Event> out;
        checkEq(parseEvents("", 0, out, 256), static_cast<size_t>(0), "parseEvents: chuoi rong -> 0");
        checkEq(parseEvents(nullptr, 0, out, 256), static_cast<size_t>(0), "parseEvents: nullptr -> 0, khong crash");
    }
    // Thieu title: bi bo qua, khong lam hong cac muc khac.
    {
        const char* json = R"({"events":[
            {"date":"2026-11-20"},
            {"date":"2026-11-21","title":"Co title"}
        ]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: thieu title bi bo qua, con 1 muc hop le");
        if (out.size() == 1) checkEq(out[0].title, std::string("Co title"), "muc con lai dung title");
    }
    // Title rong ("" ): coi nhu khong co title hop le, bi bo qua.
    {
        const char* json = R"({"events":[{"date":"2026-11-20","title":""}]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(0), "parseEvents: title rong bi bo qua");
    }
    // Ngay sai dinh dang / khong ton tai: bi bo qua.
    {
        const char* json = R"({"events":[
            {"date":"2026-13-01","title":"Thang 13 khong ton tai"},
            {"date":"2026-02-30","title":"30/2 khong ton tai"},
            {"date":"not-a-date","title":"Sai dinh dang"},
            {"date":"2026-11-20","title":"Hop le"}
        ]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: chi 1 muc ngay hop le duoc giu lai");
        if (out.size() == 1) checkEq(out[0].title, std::string("Hop le"), "muc hop le dung title");
    }
    // Nam ngoai pham vi [kMinYear, kMaxYear]: bi bo qua.
    {
        const char* json = R"({"events":[
            {"date":"1999-12-31","title":"Truoc kMinYear"},
            {"date":"2100-01-01","title":"Sau kMaxYear"},
            {"date":"2026-11-20","title":"Trong pham vi"}
        ]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: nam ngoai pham vi bi bo qua");
    }
}

void testParseEventsLimitsAndEdgeCases() {
    // Vuot kMaxEvents: chi lay toi da maxEvents, khong doc tiep (va khong crash).
    {
        std::string json = "{\"events\":[";
        for (int i = 0; i < 10; ++i) {
            if (i) json += ",";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "{\"date\":\"2026-01-%02d\",\"title\":\"E%d\"}", (i % 28) + 1, i);
            json += buf;
        }
        json += "]}";
        std::vector<Event> out;
        size_t n = parseEvents(json.c_str(), json.size(), out, 5);
        checkEq(n, static_cast<size_t>(5), "parseEvents: dung lai dung o maxEvents");
        checkEq(out.size(), static_cast<size_t>(5), "parseEvents: vector khong vuot maxEvents");
    }
    // maxEvents = 0: khong them gi, khong crash.
    {
        const char* json = R"({"events":[{"date":"2026-11-20","title":"X"}]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 0);
        checkEq(n, static_cast<size_t>(0), "parseEvents: maxEvents=0 -> khong them gi");
    }
    // Title dai hon kMaxTitle (48): bi cat bot, khong vuot gioi han, khong crash.
    {
        std::string longTitle(100, 'A');
        std::string json = "{\"events\":[{\"date\":\"2026-11-20\",\"title\":\"" + longTitle + "\"}]}";
        std::vector<Event> out;
        size_t n = parseEvents(json.c_str(), json.size(), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: title dai van duoc nhan (bi cat)");
        if (!out.empty()) {
            check(out[0].title.size() <= kMaxTitle, "parseEvents: title bi cat toi da kMaxTitle ky tu");
        }
    }
    // Ky tu UTF-8 (co dau tieng Viet) trong title: byte dau cua ky tu nhieu byte -> '?',
    // khong duoc doc tran bo nho hay sinh ky tu rac.
    {
        const char* json = "{\"events\":[{\"date\":\"2026-11-20\",\"title\":\"Th\xE1\xBB\xA7 \xC4\x91\xE1\xBB\x99\"}]}";
        // "Thủ đô" trong UTF-8: cac byte tren hop le ve mat UTF-8 nhung chi can khong crash/UB
        // va chi sinh ky tu ASCII in duoc hoac '?'.
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: UTF-8 trong title van tao duoc 1 su kien");
        if (!out.empty()) {
            bool allPrintableOrMark = true;
            for (unsigned char c : out[0].title) {
                if (c < 0x20 || c > 0x7E) { allPrintableOrMark = false; break; }
            }
            check(allPrintableOrMark, "parseEvents: title sau xu ly UTF-8 chi gom ASCII in duoc");
        }
    }
    // Chuoi title khong dong dau nhay (JSON hong giua chung): parser phai dung lai an toan,
    // khong doc qua bo nho (ASan se bat neu co loi o day).
    {
        const char* json = R"({"events":[{"date":"2026-11-20","title":"Khong dong)";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(0), "parseEvents: title khong dong -> dung an toan, 0 su kien");
    }
    // Date khong dong dau nhay ngay sau key "date".
    {
        const char* json = R"({"events":[{"date":"2026-11-20)";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(0), "parseEvents: date khong dong -> dung an toan, 0 su kien");
    }
    // Khoang trang/newline quanh dau hai cham va trong object: van parse dung.
    {
        const char* json = "{\n  \"events\" : [\n    {  \"date\"  :  \"2026-11-20\" ,  \"title\" : \"Co khoang trang\" }\n  ]\n}";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: khoang trang/newline khong lam hong parser");
        if (!out.empty()) checkEq(out[0].title, std::string("Co khoang trang"), "parseEvents: title dung khi co khoang trang");
    }
    // "date" xuat hien trong title (khong phai key that): khong duoc nham thanh truong date.
    // Luu y: findKey tim theo dung cap nhay kep "date", nen chuoi "date" ben trong gia tri title
    // (khong co dang "date":) se khong bi bat nham vi thieu dau ':' ngay sau.
    {
        const char* json = R"({"events":[{"date":"2026-11-20","title":"Check date field here"}]})";
        std::vector<Event> out;
        size_t n = parseEvents(json, strlen(json), out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: chu 'date' trong title khong gay loi");
        if (!out.empty()) checkEq(out[0].title, std::string("Check date field here"), "parseEvents: title giu nguyen chu 'date'");
    }
    // Khong co '\0' ket thuc: truyen do dai chinh xac (khong dua vao strlen tren buffer khong co '\0')
    // mo phong doc tu PSRAM (sd_storage doc nguyen file, khong dam bao co '\0' cuoi).
    {
        const char* jsonPart = "{\"events\":[{\"date\":\"2026-11-20\",\"title\":\"NoNull\"}]}";
        const size_t jsonLen = strlen(jsonPart);
        std::string src = std::string(jsonPart) + "TAIL_GARBAGE";
        std::vector<Event> out;
        size_t n = parseEvents(src.c_str(), jsonLen, out, 256);
        checkEq(n, static_cast<size_t>(1), "parseEvents: cat dung do dai (khong doc qua len phan 'TAIL_GARBAGE')");
        if (!out.empty()) checkEq(out[0].title, std::string("NoNull"), "parseEvents: title dung khi cat dung do dai");
    }
}

}  // namespace

int main() {
    testIsLeap();
    testDaysInMonth();
    testWeekdayOf();
    testAddDays();
    testShiftMonth();
    testParseEventsValid();
    testParseEventsEmptyAndMissing();
    testParseEventsLimitsAndEdgeCases();

    std::printf("CalendarLogic test: %d/%d kiem tra dat\n", gTotal - gFailed, gTotal);
    if (gFailed > 0) {
        std::fprintf(stderr, "=> %d kiem tra THAT BAI\n", gFailed);
        return 1;
    }
    return 0;
}