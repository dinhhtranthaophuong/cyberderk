// Sketch test DS3231 trên board (KHÔNG thuộc firmware chính). Env PlatformIO: rtc_test.
// Chạy chính services/time_service.cpp nên test đúng code sẽ dùng trong firmware.
// Không dùng WiFi/NTP: chỉ RTC độc lập và chỉnh giờ thủ công.
//
// Build + nạp + mở serial:
//   pio run -e rtc_test -t upload
//   pio device monitor -e rtc_test
//
// Lệnh (gõ rồi Enter; có thể gửi tự động từ laptop bằng tools/rtc_test/sync_time.py):
//   s YYYY-MM-DD HH:MM:SS   đặt giờ địa phương, ví dụ: s 2026-11-12 16:20:00
//   t                       in giờ hiện tại
//   r                       đọc thanh ghi thô DS3231 + nhiệt độ chip + cờ OSF, đối chiếu với TimeService::now()
//   i                       in lại kết quả begin() và quét I2C (dùng khi mở monitor muộn, lỡ log lúc khởi động)
//   p                       bật/tắt dòng giờ mỗi giây
//   h                       trợ giúp
//
// Kịch bản kiểm tra gợi ý:
//   A. Chạy đều:    để chạy vài phút, không có dòng CANH BAO; dòng [drift] mỗi 60 s.
//   B. Giữ giờ:     đặt giờ, rút USB 1 phút (pin CR2032 vẫn lắp), cắm lại: giờ vẫn đúng, begin() = true.
//   C. Mất pin:     rút USB, tháo pin, đợi ~10 s, lắp pin, cắm USB: begin() = false, lệnh r báo OSF=1;
//                   đặt giờ bằng lệnh s xong thì OSF về 0; reset board: begin() = true.
//   D. Không chip:  rút dây SDA hoặc SCL rồi reset: begin() = false, quét I2C báo không thấy 0x68.
//
// Lưu ý: sketch quét I2C và đọc thanh ghi thô trực tiếp bằng Wire, ngoài mutex của TimeService.
// Chỉ an toàn vì sketch này chạy một task; trong firmware chính app KHÔNG được làm vậy.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Arduino.h>
#include <Wire.h>

#include "config/pins.hpp"
#include "services/time_service.hpp"

namespace {

constexpr uint8_t kRtcAddr = 0x68;
constexpr uint32_t kPollMs = 25;            // now() tự cache 200 ms nên I2C chỉ bị đọc ~5 lần/giây
constexpr uint32_t kStartTimeoutMs = 3000;  // không thấy monitor thì vẫn tự chạy sau 3 giây
constexpr size_t kRegCount = 0x13;

bool gStarted = false;
bool gBeginOk = false;
bool gPrint = true;
bool gOverflow = false;
uint32_t gBootMs = 0;
uint32_t gLastPollMs = 0;

char gLine[80];
size_t gLen = 0;

bool gHaveLast = false;
bool gHaveBase = false;
int64_t gLastSec = 0;
int64_t gBaseSec = 0;
uint32_t gBaseMs = 0;
uint32_t gLines = 0;

const char* const kDay[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

// ---------- tính toán ----------

int64_t daysFromCivil(int y, unsigned m, unsigned d) {   // ngày kể từ 1970-01-01, không phụ thuộc múi giờ/libc
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t toSec(const DateTime& d) {
  return daysFromCivil(d.year, d.month, d.day) * 86400 + d.hour * 3600 + d.minute * 60 + d.second;
}

bool bcdOk(uint8_t v) { return (v & 0x0F) <= 9 && (v >> 4) <= 9; }
int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

bool validInput(int y, int mo, int d, int h, int mi, int s) {
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || h < 0 || h > 23 || mi < 0 || mi > 59 ||
      s < 0 || s > 59)
    return false;
  static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return d <= dim[mo - 1] + ((mo == 2 && leap) ? 1 : 0);
}

// ---------- I2C thô (chỉ để chẩn đoán) ----------

bool readRaw(uint8_t* b) {
  Wire.beginTransmission(kRtcAddr);
  Wire.write(static_cast<uint8_t>(0));
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(kRtcAddr, static_cast<uint8_t>(kRegCount)) != kRegCount) return false;
  for (size_t i = 0; i < kRegCount; i++) b[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

void scanI2c() {
  Serial.printf("Quet I2C (SDA=GPIO%d, SCL=GPIO%d):\n", Pins::I2cSda, Pins::I2cScl);
  int found = 0;
  bool rtc = false;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission(true) == 0) {
      Serial.printf("  thay thiet bi o 0x%02X%s\n", a,
                    a == 0x68 ? " (DS3231)" : (a == 0x57 ? " (EEPROM AT24C32 tren module, khong dung)" : ""));
      found++;
      if (a == kRtcAddr) rtc = true;
    }
  }
  if (found == 0) Serial.printf("  khong thay thiet bi nao\n");
  if (!rtc)
    Serial.printf("  KHONG thay DS3231 o 0x68: kiem tra day SDA/SCL, VCC (3V3), GND\n");
}

void printTime(const char* tag, const DateTime& d) {
  Serial.printf("%s%04u-%02u-%02u %02u:%02u:%02u (%s)\n", tag, d.year, d.month, d.day, d.hour,
                d.minute, d.second, kDay[d.weekday % 7]);
}

void dumpRaw() {
  uint8_t r[kRegCount];
  if (!readRaw(r)) {
    Serial.printf("LOI: khong doc duoc thanh ghi DS3231\n");
    return;
  }
  Serial.printf("thanh ghi 00-06: %02X %02X %02X %02X %02X %02X %02X\n", r[0], r[1], r[2], r[3], r[4],
                r[5], r[6]);

  bool bcdValid = bcdOk(r[0] & 0x7F) && bcdOk(r[1] & 0x7F) && bcdOk(r[2] & ((r[2] & 0x40) ? 0x1F : 0x3F)) &&
                  bcdOk(r[4] & 0x3F) && bcdOk(r[5] & 0x1F) && bcdOk(r[6]);
  char hour[12];
  if (r[2] & 0x40)
    snprintf(hour, sizeof(hour), "%02X%s", r[2] & 0x1F, (r[2] & 0x20) ? "PM" : "AM");
  else
    snprintf(hour, sizeof(hour), "%02X", r[2] & 0x3F);
  Serial.printf("gio trong chip : %s%02X-%02X-%02X %s:%02X:%02X (%s)%s\n", (r[5] & 0x80) ? "21" : "20",
                r[6], r[5] & 0x1F, r[4], hour, r[1] & 0x7F, r[0] & 0x7F,
                (r[2] & 0x40) ? "che do 12h" : "che do 24h", bcdValid ? "" : "  <- BCD HONG");

  uint8_t st = r[0x0F];
  Serial.printf("control 0E=%02X, status 0F=%02X: OSF=%d (1 = tung mat nguon), BSY=%d, A1F=%d, A2F=%d\n",
                r[0x0E], st, (st >> 7) & 1, (st >> 2) & 1, st & 1, (st >> 1) & 1);
  float temp = static_cast<int8_t>(r[0x11]) + (r[0x12] >> 6) * 0.25f;
  Serial.printf("nhiet do chip  : %.2f C\n", temp);

  DateTime now = TimeService::now();
  printTime("TimeService::now(): ", now);
  if (st & 0x80) {
    Serial.printf("=> OSF=1: service dung gio he thong, KHONG tin gio trong chip. Dat gio bang lenh s\n");
  } else if (bcdValid && !(r[2] & 0x40)) {
    DateTime c{};
    c.year = static_cast<uint16_t>(((r[5] & 0x80) ? 2100 : 2000) + bcd(r[6]));
    c.month = static_cast<uint8_t>(bcd(r[5] & 0x1F));
    c.day = static_cast<uint8_t>(bcd(r[4] & 0x3F));
    c.hour = static_cast<uint8_t>(bcd(r[2] & 0x3F));
    c.minute = static_cast<uint8_t>(bcd(r[1] & 0x7F));
    c.second = static_cast<uint8_t>(bcd(r[0] & 0x7F));
    long diff = static_cast<long>(toSec(now) - toSec(c));
    if (diff >= -2 && diff <= 2)
      Serial.printf("=> khop: service dang doc tu chip (lech %ld s la binh thuong do cache 200 ms)\n", diff);
    else
      Serial.printf("=> LECH %ld s giua service va chip\n", diff);
  }
}

void printHelp() {
  Serial.printf("Lenh (go roi Enter):\n");
  Serial.printf("  s YYYY-MM-DD HH:MM:SS  dat gio dia phuong, vd: s 2026-11-12 16:20:00\n");
  Serial.printf("  t  in gio hien tai\n");
  Serial.printf("  r  doc thanh ghi tho DS3231, nhiet do, co OSF, doi chieu voi service\n");
  Serial.printf("  i  in lai ket qua begin() va quet I2C\n");
  Serial.printf("  p  bat/tat dong gio moi giay\n");
  Serial.printf("  h  tro giup\n");
}

void printInfo() {
  Serial.printf("TimeService::begin() = %s\n", gBeginOk ? "true" : "false");
  if (gBeginOk)
    Serial.printf("  DS3231 co mat va gio trong chip dang tin\n");
  else
    Serial.printf("  khong co chip, hoac chip mat nguon (OSF), hoac gio hong: dang dung gio he thong. "
                  "Xem lenh r, dat gio bang lenh s\n");
  scanI2c();
}

// ---------- lệnh ----------

void cmdSet(const char* arg) {
  int y, mo, d, h, mi, s;
  if (sscanf(arg, " %d-%d-%d%*[ T]%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
    Serial.printf("LOI: sai cu phap. Dung: s YYYY-MM-DD HH:MM:SS\n");
    return;
  }
  if (!validInput(y, mo, d, h, mi, s)) {
    Serial.printf("LOI: gio khong hop le (nam 2000-2099, ngay phai ton tai)\n");
    return;
  }
  DateTime dt{};
  dt.year = static_cast<uint16_t>(y);
  dt.month = static_cast<uint8_t>(mo);
  dt.day = static_cast<uint8_t>(d);
  dt.hour = static_cast<uint8_t>(h);
  dt.minute = static_cast<uint8_t>(mi);
  dt.second = static_cast<uint8_t>(s);   // weekday do service tự tính
  TimeService::set(dt);
  gHaveLast = false;                      // giờ vừa nhảy, đừng báo CANH BAO
  gHaveBase = false;
  Serial.printf("OK set %04d-%02d-%02d %02d:%02d:%02d\n", y, mo, d, h, mi, s);
  dumpRaw();                              // đọc lại từ chip để xác nhận ghi thành công
}

void handleLine(char* line) {
  while (*line == ' ' || *line == '\t') line++;
  if (*line == '\0') return;
  const char* rest = line + 1;
  switch (tolower(static_cast<unsigned char>(*line))) {
    case 's': cmdSet(rest); break;
    case 't': printTime("", TimeService::now()); break;
    case 'r': dumpRaw(); break;
    case 'i': printInfo(); break;
    case 'p':
      gPrint = !gPrint;
      Serial.printf("dong gio moi giay: %s\n", gPrint ? "BAT" : "TAT");
      break;
    case 'h':
    case '?': printHelp(); break;
    default: Serial.printf("LOI: lenh khong biet. Go h de xem tro giup\n");
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (gOverflow) {
        Serial.printf("LOI: lenh qua dai\n");
        gOverflow = false;
        gLen = 0;
      } else if (gLen > 0) {
        gLine[gLen] = '\0';
        gLen = 0;
        handleLine(gLine);
      }
      continue;
    }
    if (gLen < sizeof(gLine) - 1)
      gLine[gLen++] = static_cast<char>(c);
    else
      gOverflow = true;
  }
}

// ---------- đọc giờ mỗi giây ----------

void pollClock() {
  DateTime d = TimeService::now();
  int64_t sec = toSec(d);
  if (gHaveLast && sec == gLastSec) return;

  uint32_t ms = millis();
  if (gHaveLast) {
    long delta = static_cast<long>(sec - gLastSec);
    if (delta != 1) {
      Serial.printf("CANH BAO: RTC nhay %ld giay\n", delta);
      gHaveBase = false;         // giờ nhảy thì phép đo trôi cũ vô nghĩa, đo lại từ đầu
    } else if (!gHaveBase) {     // mốc đo trôi: chỉ lấy ở lần chuyển giây BÌNH THƯỜNG (+1 s), vì lần nhảy không nằm đúng ranh giới giây
      gHaveBase = true;
      gBaseSec = sec;
      gBaseMs = ms;
      gLines = 0;
    }
  }
  gLastSec = sec;
  gHaveLast = true;
  gLines++;

  if (gPrint) printTime("", d);
  if (gHaveBase && gLines % 60 == 0) {
    long rtcMs = static_cast<long>((sec - gBaseSec) * 1000);
    long clkMs = static_cast<long>(ms - gBaseMs);
    Serial.printf("[drift] sau %ld s: millis() - RTC = %+ld ms (do phan giai ~ +-250 ms, "
                  "DS3231 chi lech ~2 ppm nen can chay nhieu gio moi thay)\n",
                  rtcMs / 1000, clkMs - rtcMs);
  }
}

void startOnce() {
  Serial.printf("=== RTC test (DS3231) ===\n");
  gBeginOk = TimeService::begin();
  printInfo();
  printHelp();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  gBootMs = millis();
}

void loop() {
  if (!gStarted) {
    if (Serial || millis() - gBootMs >= kStartTimeoutMs) {
      gStarted = true;
      startOnce();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }
  pollSerial();
  uint32_t ms = millis();
  if (ms - gLastPollMs >= kPollMs) {
    gLastPollMs = ms;
    pollClock();
  }
  vTaskDelay(pdMS_TO_TICKS(1));
}