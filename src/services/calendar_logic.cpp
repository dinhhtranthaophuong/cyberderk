#include "services/calendar_logic.hpp"

#include <cstring>

namespace CalendarLogic {

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    return (m == 2 && isLeap(y)) ? 29 : kDays[m - 1];
}

int weekdayOf(int y, int m, int d) {  // Sakamoto, 0 = Chu Nhat
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

bool addDays(int& y, int& m, int& d, int delta) {
    int ny = y, nm = m, nd = d;
    while (delta > 0) {
        if (++nd > daysInMonth(ny, nm)) {
            nd = 1;
            if (++nm > 12) { nm = 1; ++ny; }
        }
        --delta;
    }
    while (delta < 0) {
        if (--nd < 1) {
            if (--nm < 1) { nm = 12; --ny; }
            nd = daysInMonth(ny, nm);
        }
        ++delta;
    }
    if (ny < kMinYear || ny > kMaxYear) return false;
    y = ny; m = nm; d = nd;
    return true;
}

bool shiftMonth(int& y, int& m, int& d, int delta) {
    int idx = y * 12 + (m - 1) + delta;
    int ny = idx / 12, nm = idx % 12 + 1;
    if (ny < kMinYear || ny > kMaxYear) return false;
    y = ny; m = nm;
    int dim = daysInMonth(ny, nm);
    if (d > dim) d = dim;
    return true;
}

namespace {

// Tim `"key"` trong [from, to), roi `:` roi `"`. Tra vi tri dau nhay mo cua gia tri, hoac npos.
size_t findKey(const char* s, size_t len, size_t from, size_t to, const char* key) {
    const size_t klen = strlen(key);
    if (to > len) to = len;
    for (size_t i = from; i + klen + 2 <= to; ++i) {
        if (s[i] != '"' || strncmp(s + i + 1, key, klen) != 0 || s[i + 1 + klen] != '"') continue;
        size_t p = i + klen + 2;
        while (p < to && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
        if (p >= to || s[p] != ':') continue;
        ++p;
        while (p < to && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
        if (p < to && s[p] == '"') return p;
    }
    return std::string::npos;
}

// Doc chuoi bat dau tai dau nhay mo. Chi giu ASCII in duoc: byte dau cua ky tu UTF-8 -> '?',
// byte tiep theo bo qua (font mac dinh khong co dau tieng Viet). Tra false neu chuoi khong dong.
bool readQuoted(const char* s, size_t len, size_t quotePos, std::string& out, size_t& after) {
    out.clear();
    size_t i = quotePos + 1;
    while (i < len && s[i] != '"') {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char ch = 0;
        if (c == '\\' && i + 1 < len) {
            char n = s[i + 1];
            ch = (n == 'n' || n == 't' || n == 'r') ? ' ' : n;
            i += 2;
        } else {
            ++i;
            if (c >= 0xC0) ch = '?';
            else if (c >= 0x80) ch = 0;  // byte tiep theo cua UTF-8
            else if (c < 0x20) ch = ' ';
            else ch = static_cast<char>(c);
        }
        if (ch != 0 && out.size() < kMaxTitle) out.push_back(ch);
    }
    if (i >= len) return false;
    after = i + 1;
    return true;
}

bool parseDate(const std::string& s, int& y, int& m, int& d) {
    if (s.size() < 10 || s[4] != '-' || s[7] != '-') return false;
    auto dig = [&](size_t i) { return (s[i] >= '0' && s[i] <= '9') ? s[i] - '0' : -1000; };
    y = dig(0) * 1000 + dig(1) * 100 + dig(2) * 10 + dig(3);
    m = dig(5) * 10 + dig(6);
    d = dig(8) * 10 + dig(9);
    if (y < kMinYear || y > kMaxYear || m < 1 || m > 12) return false;
    return d >= 1 && d <= daysInMonth(y, m);
}

}  // namespace

size_t parseEvents(const char* text, size_t len, std::vector<Event>& out, size_t maxEvents) {
    if (!text || len == 0) return 0;
    size_t added = 0, pos = 0;
    while (out.size() < maxEvents) {
        size_t k = findKey(text, len, pos, len, "date");
        if (k == std::string::npos) break;
        std::string date;
        size_t after = 0;
        if (!readQuoted(text, len, k, date, after)) break;
        pos = after;

        size_t objStart = k;
        while (objStart > 0 && text[objStart] != '{') --objStart;
        size_t objEnd = k;
        while (objEnd < len && text[objEnd] != '}') ++objEnd;

        int y, m, d;
        if (!parseDate(date, y, m, d)) continue;
        size_t t = findKey(text, len, objStart, objEnd, "title");
        if (t == std::string::npos) continue;
        std::string title;
        size_t tafter = 0;
        if (!readQuoted(text, len, t, title, tafter) || title.empty()) continue;

        Event ev;
        ev.year = static_cast<uint16_t>(y);
        ev.month = static_cast<uint8_t>(m);
        ev.day = static_cast<uint8_t>(d);
        ev.title = title;
        out.push_back(ev);
        ++added;
    }
    return added;
}

}  // namespace CalendarLogic
