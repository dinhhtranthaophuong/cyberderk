// Test máy tính cho services/time_service.cpp (không thuộc firmware).
// Dùng bộ giả lập trong stubs/ (DS3231 giả, đồng hồ giả, WiFi/SNTP giả). KHÔNG đụng đồng hồ thật.
// Test include thẳng time_service.cpp để reset được trạng thái nội bộ giữa các kịch bản.
//
// Chạy từ thư mục gốc dự án (mỗi lệnh trên một dòng):
//   g++ -std=c++17 -g -O1 -Wall -fsanitize=address,undefined -fno-omit-frame-pointer -I tools/time_test/stubs -I src tools/time_test/time_test.cpp -o /tmp/time_test -pthread && /tmp/time_test
// Kiểm tra đua dữ liệu (ThreadSanitizer, không chạy chung với ASan):
//   g++ -std=c++17 -g -O1 -Wall -fsanitize=thread -I tools/time_test/stubs -I src tools/time_test/time_test.cpp -o /tmp/time_test_tsan -pthread && /tmp/time_test_tsan
// Đặt biến môi trường TIME_TEST_VERBOSE=1 để in log Serial giả.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "services/time_service.cpp"

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (c) {                                                             \
      ++g_pass;                                                          \
    } else {                                                             \
      ++g_fail;                                                          \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);           \
    }                                                                    \
  } while (0)

// ---------- tiện ích ----------
static bool same(const DateTime& a, const DateTime& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour &&
         a.minute == b.minute && a.second == b.second && a.weekday == b.weekday;
}

static DateTime mk(int y, int m, int d, int h, int mi, int s) {   // chỉ cho ngày hợp lệ
  DateTime x{};
  x.year = static_cast<uint16_t>(y);
  x.month = static_cast<uint8_t>(m);
  x.day = static_cast<uint8_t>(d);
  x.hour = static_cast<uint8_t>(h);
  x.minute = static_cast<uint8_t>(mi);
  x.second = static_cast<uint8_t>(s);
  x.weekday = weekdayOf(x.year, x.month, x.day);
  return x;
}

// Epoch của giờ địa phương ICT-7 (chỉ đúng khi TZ = ICT-7).
static time_t localEpoch(const DateTime& d) {
  tm t{};
  t.tm_year = d.year - 1900;
  t.tm_mon = d.month - 1;
  t.tm_mday = d.day;
  t.tm_hour = d.hour;
  t.tm_min = d.minute;
  t.tm_sec = d.second;
  return timegm(&t) - 7 * 3600;
}

static void setRegs(std::initializer_list<uint8_t> v) {
  size_t i = 0;
  for (uint8_t b : v) Wire.regs[i++] = b;
}
static bool regsAre(std::initializer_list<uint8_t> v) {
  size_t i = 0;
  for (uint8_t b : v)
    if (Wire.regs[i++] != b) return false;
  return true;
}
static std::array<uint8_t, 0x13> snapshot() {
  std::array<uint8_t, 0x13> a{};
  std::memcpy(a.data(), Wire.regs, a.size());
  return a;
}
static bool logHas(const char* s) {
  std::lock_guard<std::mutex> g(sim::logMu);
  for (const auto& l : sim::log)
    if (l.find(s) != std::string::npos) return true;
  return false;
}
static void advance(uint32_t ms) { sim::millisNow.fetch_add(ms); }
static time_t sysEpoch() { return fake_time(nullptr); }

// Đưa module về trạng thái "chưa begin()" và dựng lại môi trường giả.
static void resetAll(bool chipPresent) {
  if (gMutex) {
    delete static_cast<std::recursive_mutex*>(gMutex);
    gMutex = nullptr;
  }
  gInited = false;
  gBeginResult = false;
  gChipPresent = false;
  gTrusted = false;
  gCacheValid = false;
  gCache = DateTime{};
  gCacheAt = 0;
  Wire.reset();
  Wire.chipPresent = chipPresent;
  sim::reset();
  setenv("TZ", "ICT-7", 1);
  tzset();
}

// 2026-11-12 13:30:45 (Thứ Năm), thanh ghi viết tay độc lập với encodeTime.
static void loadThu() {
  setRegs({0x45, 0x30, 0x13, 0x05, 0x12, 0x11, 0x26});
  Wire.regs[0x0F] = 0x00;
}

// ---------- 1. tính toán thuần ----------
static void testPure() {
  std::printf("[1] tinh toan thuan\n");
  for (int v = 0; v <= 99; v++) {
    CHECK(bcd2bin(bin2bcd(static_cast<uint8_t>(v))) == v);
    CHECK(bcdOk(bin2bcd(static_cast<uint8_t>(v))));
  }
  for (uint8_t v : {0x0A, 0x1F, 0xA0, 0xFF, 0x9A}) CHECK(!bcdOk(v));
  CHECK(bin2bcd(59) == 0x59 && bin2bcd(7) == 0x07 && bin2bcd(0) == 0x00);

  CHECK(!isLeap(1900) && isLeap(2000) && !isLeap(2100) && isLeap(2024) && !isLeap(2026));
  CHECK(daysInMonth(2024, 2) == 29 && daysInMonth(2026, 2) == 28 && daysInMonth(2100, 2) == 28);
  CHECK(daysInMonth(2026, 4) == 30 && daysInMonth(2026, 12) == 31);
  CHECK(daysInMonth(2026, 0) == 0 && daysInMonth(2026, 13) == 0);

  // Thứ trong tuần: giá trị đã biết
  CHECK(weekdayOf(2000, 1, 1) == 6);    // Thứ Bảy
  CHECK(weekdayOf(2000, 3, 1) == 3);    // Thứ Tư
  CHECK(weekdayOf(2024, 2, 29) == 4);   // Thứ Năm
  CHECK(weekdayOf(2026, 11, 12) == 4);  // Thứ Năm
  CHECK(weekdayOf(2099, 12, 31) == 4);  // Thứ Năm

  // Quét toàn bộ 2000-2099 so với libc; đồng thời kiểm tra daysInMonth
  for (int y = 2000; y <= 2099; y++) {
    for (int m = 1; m <= 12; m++) {
      int dim = daysInMonth(static_cast<uint16_t>(y), static_cast<uint8_t>(m));
      for (int d = 1; d <= dim; d++) {
        tm t{};
        t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = 12;
        time_t e = timegm(&t);
        tm g{};
        gmtime_r(&e, &g);
        CHECK(weekdayOf(static_cast<uint16_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d)) ==
              g.tm_wday);
      }
      tm over{};
      over.tm_year = y - 1900; over.tm_mon = m - 1; over.tm_mday = dim + 1; over.tm_hour = 12;
      timegm(&over);
      CHECK(over.tm_mday == 1 && over.tm_mon != m - 1);   // ngày dim+1 phải tràn sang tháng sau
    }
  }

  // valid()
  CHECK(valid(mk(2000, 1, 1, 0, 0, 0)));
  CHECK(valid(mk(2099, 12, 31, 23, 59, 59)));
  CHECK(valid(mk(2024, 2, 29, 12, 0, 0)));
  auto bad = [](int y, int m, int d, int h, int mi, int s) {
    DateTime x{};
    x.year = static_cast<uint16_t>(y); x.month = static_cast<uint8_t>(m); x.day = static_cast<uint8_t>(d);
    x.hour = static_cast<uint8_t>(h); x.minute = static_cast<uint8_t>(mi); x.second = static_cast<uint8_t>(s);
    return !valid(x);
  };
  CHECK(bad(1999, 12, 31, 0, 0, 0));
  CHECK(bad(2100, 1, 1, 0, 0, 0));
  CHECK(bad(2026, 0, 1, 0, 0, 0));
  CHECK(bad(2026, 13, 1, 0, 0, 0));
  CHECK(bad(2026, 1, 0, 0, 0, 0));
  CHECK(bad(2026, 4, 31, 0, 0, 0));
  CHECK(bad(2023, 2, 29, 0, 0, 0));
  CHECK(bad(2100, 2, 29, 0, 0, 0));
  CHECK(bad(2026, 1, 1, 24, 0, 0));
  CHECK(bad(2026, 1, 1, 0, 60, 0));
  CHECK(bad(2026, 1, 1, 0, 0, 60));
}

// ---------- 2. encode / decode ----------
static void testCodec() {
  std::printf("[2] encode/decode\n");
  // Mọi ngày 2000-2099 (giờ thay đổi theo ngày)
  for (int y = 2000; y <= 2099; y++)
    for (int m = 1; m <= 12; m++)
      for (int d = 1; d <= daysInMonth(static_cast<uint16_t>(y), static_cast<uint8_t>(m)); d++) {
        DateTime in = mk(y, m, d, (d * 7) % 24, (d * 13) % 60, (d * 29) % 60), out{};
        uint8_t r[7];
        encodeTime(in, r);
        CHECK(decodeTime(r, &out));
        CHECK(same(in, out));
        CHECK(r[3] == in.weekday + 1 && (r[2] & 0x40) == 0 && (r[5] & 0x80) == 0);
      }
  // Mọi giờ/phút/giây của một ngày
  for (int h = 0; h < 24; h++)
    for (int mi = 0; mi < 60; mi++)
      for (int s = 0; s < 60; s += 7) {
        DateTime in = mk(2026, 11, 12, h, mi, s), out{};
        uint8_t r[7];
        encodeTime(in, r);
        CHECK(decodeTime(r, &out) && same(in, out));
      }

  DateTime out{};
  uint8_t r[7];
  auto base = [&](uint8_t hourReg) { const uint8_t b[7] = {0x00, 0x00, hourReg, 0x01, 0x01, 0x01, 0x26}; std::memcpy(r, b, 7); };

  // Chế độ 12 giờ (chip bị ghi bởi thiết bị khác)
  base(0x52); CHECK(decodeTime(r, &out) && out.hour == 0);    // 12 AM
  base(0x72); CHECK(decodeTime(r, &out) && out.hour == 12);   // 12 PM
  base(0x41); CHECK(decodeTime(r, &out) && out.hour == 1);    // 1 AM
  base(0x61); CHECK(decodeTime(r, &out) && out.hour == 13);   // 1 PM
  base(0x51); CHECK(decodeTime(r, &out) && out.hour == 11);   // 11 AM
  base(0x71); CHECK(decodeTime(r, &out) && out.hour == 23);   // 11 PM
  base(0x40); CHECK(!decodeTime(r, &out));                    // 0 giờ ở chế độ 12h: sai
  base(0x53); CHECK(!decodeTime(r, &out));                    // 13 ở chế độ 12h: sai
  base(0x24); CHECK(!decodeTime(r, &out));                    // 24 giờ
  base(0x1A); CHECK(!decodeTime(r, &out));                    // BCD hỏng
  base(0x23); CHECK(decodeTime(r, &out) && out.hour == 23);

  // Các trường khác
  base(0x00); r[0] = 0x80 | 0x15; CHECK(decodeTime(r, &out) && out.second == 15);   // bit cao của giây bị che
  base(0x00); r[0] = 0x60; CHECK(!decodeTime(r, &out));
  base(0x00); r[0] = 0x6A; CHECK(!decodeTime(r, &out));
  base(0x00); r[1] = 0x7A; CHECK(!decodeTime(r, &out));
  base(0x00); r[4] = 0x00; CHECK(!decodeTime(r, &out));
  base(0x00); r[4] = 0x32; CHECK(!decodeTime(r, &out));
  base(0x00); r[5] = 0x00; CHECK(!decodeTime(r, &out));
  base(0x00); r[5] = 0x13; CHECK(!decodeTime(r, &out));
  base(0x00); r[5] = 0x81; CHECK(!decodeTime(r, &out));          // bit thế kỷ: năm 2126
  base(0x00); r[4] = 0x30; r[5] = 0x02; CHECK(!decodeTime(r, &out));   // 30/02
  base(0x00); r[6] = 0xA0; CHECK(!decodeTime(r, &out));
  base(0x00); r[3] = 0x07; CHECK(decodeTime(r, &out) && out.weekday == weekdayOf(2026, 1, 1));   // không tin thanh ghi thứ
}

// ---------- 3. begin() ----------
static void testBegin() {
  std::printf("[3] begin\n");

  // Không có chip
  resetAll(false);
  CHECK(!TimeService::begin());
  CHECK(Wire.beginCalls == 1 && Wire.lastSda == 1 && Wire.lastScl == 2 && Wire.lastFreq == 100000 &&
        Wire.timeoutMs == 50);
  CHECK(logHas("khong thay DS3231"));
  DateTime d = TimeService::now();
  CHECK(d.year == 1970 && d.month == 1 && d.day == 1 && d.hour == 7 && d.weekday == 4);   // epoch 0 theo ICT-7
  int t0 = Wire.transactions;
  (void)TimeService::now();
  CHECK(Wire.transactions == t0);              // không chip: không đụng I2C
  CHECK(!TimeService::begin() && Wire.beginCalls == 1 && Wire.transactions == t0);   // gọi lại: không làm gì

  // Chip hợp lệ, OSF sạch
  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  CHECK(logHas("DS3231 OK"));
  CHECK(sysEpoch() == localEpoch(mk(2026, 11, 12, 13, 30, 45)));   // nạp vào giờ hệ thống
  CHECK(same(TimeService::now(), mk(2026, 11, 12, 13, 30, 45)));
  CHECK(TimeService::now().weekday == 4);
  CHECK(Wire.violations == 0);
  {
    int tx = Wire.transactions;
    CHECK(TimeService::begin() && Wire.transactions == tx);        // idempotent, trả kết quả lần đầu
  }

  // OSF bật (vừa mất pin): giờ chip không đáng tin
  resetAll(true);
  loadThu();
  Wire.regs[0x0F] = 0x88;                      // OSF + bit EN32kHz
  auto before = snapshot();
  CHECK(!TimeService::begin());
  CHECK(logHas("OSF"));
  CHECK(sysEpoch() == 0);                      // không nạp giờ rác vào hệ thống
  CHECK(TimeService::now().year == 1970);
  CHECK(snapshot() == before);                 // begin không ghi gì vào chip
  {
    int tx = Wire.transactions;
    CHECK(!TimeService::begin() && Wire.transactions == tx);
    (void)TimeService::now();
    CHECK(Wire.transactions == tx);            // đang không tin chip nên không đọc nó
  }
  // set() khôi phục: ghi giờ, xóa OSF nhưng giữ các bit khác
  TimeService::set(mk(2026, 5, 6, 7, 8, 9));
  CHECK(regsAre({0x09, 0x08, 0x07, 0x04, 0x06, 0x05, 0x26}));    // 6/5/2026 là Thứ Tư -> reg 4
  CHECK(Wire.regs[0x0F] == 0x08);
  Wire.regs[0] = 0x30;                         // đổi giây trong chip: nếu now() đọc chip sẽ thấy 30
  advance(250);
  CHECK(TimeService::now().second == 30);
  CHECK(TimeService::now().hour == 7 && TimeService::now().minute == 8);

  // Giờ trong chip hỏng
  resetAll(true);
  setRegs({0x45, 0x30, 0x13, 0x05, 0x12, 0x13, 0x26});            // tháng 13
  CHECK(!TimeService::begin());
  CHECK(logHas("khong hop le") && sysEpoch() == 0);
  resetAll(true);
  setRegs({0x6A, 0x30, 0x13, 0x05, 0x12, 0x11, 0x26});            // BCD hỏng
  CHECK(!TimeService::begin() && sysEpoch() == 0);
}

// ---------- 4. now(): cache và lỗi I2C ----------
static void testNow() {
  std::printf("[4] now: cache, loi I2C\n");

  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  Wire.reads = 0;
  CHECK(TimeService::now().second == 45 && Wire.reads == 1);
  advance(100);
  CHECK(TimeService::now().second == 45 && Wire.reads == 1);     // còn cache
  Wire.regs[0] = 0x46;
  advance(99);                                                   // tổng 199 ms
  CHECK(TimeService::now().second == 45 && Wire.reads == 1);
  advance(1);                                                    // tổng 200 ms: hết cache
  CHECK(TimeService::now().second == 46 && Wire.reads == 2);

  // millis quay vòng 32-bit
  sim::millisNow = 0xFFFFFF00u;
  int r0 = Wire.reads;
  (void)TimeService::now();
  CHECK(Wire.reads == r0 + 1);
  advance(100);
  (void)TimeService::now();
  CHECK(Wire.reads == r0 + 1);
  advance(200);                                                  // đã vượt qua 0
  (void)TimeService::now();
  CHECK(Wire.reads == r0 + 2);

  // Lỗi I2C: rơi về giờ hệ thống rồi tự phục hồi
  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  setRegs({0x00, 0x00, 0x14, 0x05, 0x12, 0x11, 0x26});           // chip 14:00:00, hệ thống ~13:30:45
  advance(300);
  Wire.failReadNext = 1;
  CHECK(TimeService::now().hour == 13);                          // đọc lỗi: giờ hệ thống
  advance(300);
  CHECK(TimeService::now().hour == 14);                          // phục hồi
  advance(300);
  Wire.failTxNext = 1;
  CHECK(TimeService::now().hour == 13);                          // NACK khi gửi địa chỉ thanh ghi
  advance(300);
  CHECK(TimeService::now().hour == 14);
  advance(300);
  Wire.chipPresent = false;                                      // chip bị rút
  CHECK(TimeService::now().hour == 13);
  advance(300);
  CHECK(TimeService::now().hour == 13);
  Wire.chipPresent = true;
  advance(300);
  CHECK(TimeService::now().hour == 14);
  CHECK(Wire.violations == 0);
}

// ---------- 5. set() ----------
static void testSet() {
  std::printf("[5] set\n");

  resetAll(true);
  loadThu();
  Wire.regs[0x0F] = 0x80;
  CHECK(!TimeService::begin());                                  // OSF
  TimeService::set(mk(2024, 2, 29, 23, 59, 58));
  CHECK(regsAre({0x58, 0x59, 0x23, 0x05, 0x29, 0x02, 0x24}));    // 29/2/2024 Thứ Năm
  CHECK((Wire.regs[0x0F] & 0x80) == 0);
  CHECK(sysEpoch() == localEpoch(mk(2024, 2, 29, 23, 59, 58)));

  TimeService::set(mk(2099, 12, 31, 23, 59, 59));
  CHECK(regsAre({0x59, 0x59, 0x23, 0x05, 0x31, 0x12, 0x99}));
  TimeService::set(mk(2000, 1, 1, 0, 0, 0));
  CHECK(regsAre({0x00, 0x00, 0x00, 0x07, 0x01, 0x01, 0x00}));    // Thứ Bảy -> reg 7
  CHECK((Wire.regs[0x0F] & 0x80) == 0);

  // weekday người gọi truyền sai bị bỏ qua
  DateTime wrong = mk(2026, 11, 12, 13, 30, 45);
  wrong.weekday = 6;
  TimeService::set(wrong);
  CHECK(Wire.regs[3] == 0x05);
  advance(300);
  CHECK(TimeService::now().weekday == 4);

  // Giá trị không hợp lệ: không ghi chip, không đổi giờ hệ thống
  TimeService::set(mk(2026, 1, 1, 1, 1, 1));
  std::vector<DateTime> bads;
  auto add = [&](int y, int m, int d, int h, int mi, int s) {
    DateTime x{};
    x.year = static_cast<uint16_t>(y); x.month = static_cast<uint8_t>(m); x.day = static_cast<uint8_t>(d);
    x.hour = static_cast<uint8_t>(h); x.minute = static_cast<uint8_t>(mi); x.second = static_cast<uint8_t>(s);
    bads.push_back(x);
  };
  add(1999, 12, 31, 0, 0, 0); add(2100, 1, 1, 0, 0, 0); add(2026, 0, 1, 0, 0, 0);
  add(2026, 13, 1, 0, 0, 0);  add(2026, 4, 31, 0, 0, 0); add(2023, 2, 29, 0, 0, 0);
  add(2100, 2, 29, 0, 0, 0);  add(2026, 1, 0, 0, 0, 0);  add(2026, 1, 1, 24, 0, 0);
  add(2026, 1, 1, 0, 60, 0);  add(2026, 1, 1, 0, 0, 60); add(0, 0, 0, 0, 0, 0);
  for (const auto& b : bads) {
    auto regsBefore = snapshot();
    time_t e0 = sysEpoch();
    int w0 = Wire.writes, tx0 = Wire.transactions;
    sim::log.clear();
    TimeService::set(b);
    CHECK(snapshot() == regsBefore);
    CHECK(sysEpoch() == e0);
    CHECK(Wire.writes == w0 && Wire.transactions == tx0);
    CHECK(logHas("bo qua"));
  }

  // Ghi chip lỗi: giờ hệ thống vẫn đúng, now() không dùng giờ chip cũ
  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  Wire.failTxNext = 1;
  TimeService::set(mk(2026, 6, 7, 8, 15, 30));
  CHECK(logHas("ghi DS3231 loi"));
  CHECK(Wire.regs[2] == 0x13);                                   // chip chưa đổi
  advance(300);
  CHECK(TimeService::now().hour == 8 && TimeService::now().minute == 15);
  // Lỗi khi đọc status sau khi đã ghi giờ
  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  Wire.failReadNext = 1;
  TimeService::set(mk(2026, 6, 7, 8, 15, 30));
  CHECK(logHas("ghi DS3231 loi"));
  advance(300);
  CHECK(TimeService::now().hour == 8);
  // Lần set kế tiếp thành công thì tin chip trở lại
  TimeService::set(mk(2026, 6, 7, 9, 16, 31));
  Wire.regs[0] = 0x50;
  advance(300);
  CHECK(TimeService::now().second == 50 && TimeService::now().hour == 9);

  // Không có chip: chỉ đặt giờ hệ thống
  resetAll(false);
  CHECK(!TimeService::begin());
  int tx = Wire.transactions;
  TimeService::set(mk(2026, 11, 12, 8, 0, 0));
  CHECK(Wire.transactions == tx && Wire.writes == 0);
  CHECK(same(TimeService::now(), mk(2026, 11, 12, 8, 0, 0)));

  // set() gọi trước begin(): không treo, không crash, chỉ đặt giờ hệ thống
  resetAll(true);
  TimeService::set(mk(2026, 11, 12, 9, 10, 11));
  CHECK(Wire.transactions == 0);
  CHECK(same(TimeService::now(), mk(2026, 11, 12, 9, 10, 11)));
}

// ---------- 6. NTP ----------
static void testNtp() {
  std::printf("[6] syncNtp\n");
  const time_t ntp = [] { tm t{}; t.tm_year = 126; t.tm_mon = 10; t.tm_mday = 12; t.tm_hour = 6; t.tm_min = 30; return timegm(&t); }();   // 2026-11-12 06:30:00 UTC

  // Chưa có WiFi
  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  auto before = snapshot();
  CHECK(!TimeService::syncNtp("ICT-7"));
  CHECK(sim::configCalls == 0 && snapshot() == before && logHas("chua co WiFi"));

  // Thành công, tz mặc định
  sim::wifiConnected = true;
  sim::sntpPollsLeft = 3;
  sim::ntpEpoch = ntp;
  uint32_t m0 = sim::millisNow;
  CHECK(TimeService::syncNtp(nullptr));
  uint32_t el = sim::millisNow - m0;
  CHECK(el >= 300 && el < 1000);                                 // chờ bằng vTaskDelay, không busy-wait
  CHECK(sim::lastTz == "ICT-7" && sim::resetBeforeConfig && sim::sntpStopCalls == 1);
  CHECK(regsAre({0x00, 0x30, 0x13, 0x05, 0x12, 0x11, 0x26}));    // 13:30 ICT, Thứ Năm
  CHECK(sysEpoch() == ntp);
  CHECK((Wire.regs[0x0F] & 0x80) == 0);
  advance(300);
  CHECK(TimeService::now().hour == 13 && TimeService::now().minute == 30);

  // tz rỗng cũng là mặc định; tz khác thì ghi giờ theo tz đó
  sim::sntpPollsLeft = 0;
  sim::sntpStatus = 0;
  CHECK(TimeService::syncNtp("") && sim::lastTz == "ICT-7");
  sim::sntpPollsLeft = 0;
  CHECK(TimeService::syncNtp("UTC0"));
  CHECK(sim::lastTz == "UTC0");
  CHECK(regsAre({0x00, 0x30, 0x06, 0x05, 0x12, 0x11, 0x26}));    // 06:30 UTC
  setenv("TZ", "ICT-7", 1);
  tzset();

  // Hết thời gian chờ; cờ COMPLETED cũ không được tin
  resetAll(true);
  loadThu();
  CHECK(TimeService::begin());
  sim::wifiConnected = true;
  sim::sntpStatus = SNTP_SYNC_STATUS_COMPLETED;                  // dư cờ của lần đồng bộ trước
  sim::sntpPollsLeft = -1;
  auto regsBefore = snapshot();
  time_t e0 = sysEpoch();
  m0 = sim::millisNow;
  CHECK(!TimeService::syncNtp("ICT-7"));
  el = sim::millisNow - m0;
  CHECK(el >= 8000 && el < 8200);
  CHECK(snapshot() == regsBefore);
  CHECK(sysEpoch() - e0 >= 8 && sysEpoch() - e0 <= 9);           // giờ hệ thống chỉ trôi theo thời gian, không bị đặt lại
  CHECK(sim::sntpStopCalls == 1 && logHas("het thoi gian"));

  // Đường thực tế quan trọng nhất: chip vừa mất pin, NTP đặt lại giờ
  resetAll(true);
  loadThu();
  Wire.regs[0x0F] = 0x80;
  CHECK(!TimeService::begin());
  sim::wifiConnected = true;
  sim::sntpPollsLeft = 1;
  sim::ntpEpoch = ntp;
  CHECK(TimeService::syncNtp("ICT-7"));
  CHECK((Wire.regs[0x0F] & 0x80) == 0);
  CHECK(regsAre({0x00, 0x30, 0x13, 0x05, 0x12, 0x11, 0x26}));
  Wire.regs[0] = 0x59;
  advance(300);
  CHECK(TimeService::now().second == 59);                        // đã tin chip trở lại

  // Không có chip: chỉ đổi giờ hệ thống
  resetAll(false);
  CHECK(!TimeService::begin());
  sim::wifiConnected = true;
  sim::sntpPollsLeft = 0;
  sim::ntpEpoch = ntp;
  int tx = Wire.transactions;
  CHECK(TimeService::syncNtp(nullptr));
  CHECK(Wire.transactions == tx && Wire.writes == 0);
  CHECK(TimeService::now().hour == 13 && TimeService::now().minute == 30);
}

// ---------- 7. đa luồng ----------
static void testThreads() {
  std::printf("[7] da luong\n");

  // Kiểm chứng bộ phát hiện chồng giao dịch hoạt động
  resetAll(true);
  Wire.beginTransmission(0x68);
  Wire.beginTransmission(0x68);
  CHECK(Wire.violations == 1);
  Wire.endTransmission();
  Wire.endTransmission();

  // Bốn luồng đọc + một luồng ghi. Giờ ghi luôn có hour == minute == second,
  // nên nếu có lần đọc "rách" (ghép hai lần ghi) sẽ bị phát hiện.
  resetAll(true);
  setRegs({0x05, 0x05, 0x05, 0x05, 0x01, 0x01, 0x26});
  CHECK(TimeService::begin());
  sim::autoAdvance = true;                                       // cache hết hạn đều đặn
  std::atomic<int> torn{0}, invalid{0};
  std::atomic<bool> go{false};

  auto reader = [&] {
    while (!go) std::this_thread::yield();
    for (int i = 0; i < 4000; i++) {
      DateTime d = TimeService::now();
      if (!valid(d)) invalid++;
      else if (d.hour != d.minute || d.minute != d.second) torn++;
    }
  };
  auto writer = [&] {
    while (!go) std::this_thread::yield();
    for (int i = 0; i < 2000; i++) {
      int k = i % 24;
      TimeService::set(mk(2026, 1, 2, k, k, k));
    }
  };
  std::vector<std::thread> ts;
  for (int i = 0; i < 4; i++) ts.emplace_back(reader);
  ts.emplace_back(writer);
  go = true;
  for (auto& t : ts) t.join();
  sim::autoAdvance = false;

  CHECK(torn == 0);
  CHECK(invalid == 0);
  CHECK(Wire.violations == 0);
  DateTime fin = TimeService::now();
  CHECK(valid(fin));
}

int main() {
  testPure();
  testCodec();
  testBegin();
  testNow();
  testSet();
  testNtp();
  testThreads();
  std::printf("\n%d kiem tra dat, %d that bai\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
