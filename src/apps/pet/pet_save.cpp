#include "apps/pet/pet_save.hpp"

#include <cstdio>
#include <cstring>

namespace {

const char kHeader[] = "cyberdeck-pet 1";
const char* const kKeys[7] = {"hunger", "fun", "clean", "energy", "skin", "asleep", "time"};

// Số nguyên không âm, chỉ gồm chữ số (tối đa 18 chữ số). false nếu sai.
bool parseUInt(const char* s, long long* out) {
    if (*s == '\0') return false;
    long long v = 0;
    int digits = 0;
    for (; *s != '\0'; ++s) {
        if (*s < '0' || *s > '9' || ++digits > 18) return false;
        v = v * 10 + (*s - '0');
    }
    *out = v;
    return true;
}

}  // namespace

namespace PetSave {

int64_t epochSeconds(int year, int month, int day, int hour, int minute, int second) {
    // Số ngày kể từ 1970-01-01 (thuật toán days_from_civil của Howard Hinnant).
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int mp = (month + 9) % 12;                       // tháng 3 = 0
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

size_t format(const PetSnapshot& s, int64_t epoch, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0) return 0;
    const int n = std::snprintf(buf, cap,
        "%s\nhunger=%u\nfun=%u\nclean=%u\nenergy=%u\nskin=%u\nasleep=%u\ntime=%lld\n",
        kHeader,
        static_cast<unsigned>(s.value[static_cast<int>(PetStat::HUNGER)]),
        static_cast<unsigned>(s.value[static_cast<int>(PetStat::FUN)]),
        static_cast<unsigned>(s.value[static_cast<int>(PetStat::CLEAN)]),
        static_cast<unsigned>(s.value[static_cast<int>(PetStat::ENERGY)]),
        static_cast<unsigned>(s.skin),
        s.asleep ? 1u : 0u,
        static_cast<long long>(epoch));
    return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

bool parse(const char* text, size_t len, PetSnapshot* snap, int64_t* epoch) {
    if (text == nullptr || snap == nullptr || epoch == nullptr || len == 0 || len > 512) return false;

    char buf[513];
    std::memcpy(buf, text, len);
    buf[len] = '\0';

    bool have[7] = {};
    long long val[7] = {};
    int line = 0;
    char* p = buf;
    while (true) {
        char* end = std::strchr(p, '\n');
        if (end != nullptr) *end = '\0';
        size_t l = std::strlen(p);
        if (l > 0 && p[l - 1] == '\r') p[l - 1] = '\0';

        if (line == 0) {
            if (std::strcmp(p, kHeader) != 0) return false;
        } else if (*p != '\0') {
            char* eq = std::strchr(p, '=');
            if (eq == nullptr) return false;
            *eq = '\0';
            for (int i = 0; i < 7; ++i) {
                if (std::strcmp(p, kKeys[i]) == 0) {
                    if (!parseUInt(eq + 1, &val[i])) return false;
                    have[i] = true;
                }
            }
        }
        ++line;
        if (end == nullptr) break;
        p = end + 1;
    }

    for (int i = 0; i < 7; ++i) {
        if (!have[i]) return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (val[i] > PetState::kMax) return false;
    }
    if (val[4] >= static_cast<long long>(PetSkin::COUNT)) return false;
    if (val[5] > 1) return false;

    for (int i = 0; i < 4; ++i) snap->value[i] = static_cast<uint8_t>(val[i]);
    snap->skin = static_cast<PetSkin>(val[4]);
    snap->asleep = (val[5] == 1);
    *epoch = static_cast<int64_t>(val[6]);
    return true;
}

}  // namespace PetSave
