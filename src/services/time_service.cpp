#include "services/time_service.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#include "config/pins.hpp"

namespace {

constexpr uint8_t kAddr = 0x68;               // DS3231 (EEPROM AT24C32 trên module ở 0x57, không dùng)
constexpr uint32_t kI2cHz = 100000;           // 100 kHz: ổn định với dây dupont
constexpr uint16_t kI2cTimeoutMs = 50;
constexpr uint32_t kCacheMs = 200;            // now() không đọc I2C quá 5 lần/giây
constexpr uint32_t kNtpTimeoutMs = 8000;
constexpr uint32_t kLockWaitMs = 200;
constexpr const char* kDefaultTz = "ICT-7";   // Việt Nam, UTC+7, không DST

constexpr uint8_t kRegTime = 0x00;            // 7 thanh ghi: giây..năm
constexpr uint8_t kRegStatus = 0x0F;
constexpr uint8_t kStatusOsf = 0x80;          // oscillator stopped flag: chip từng mất nguồn

SemaphoreHandle_t gMutex = nullptr;           // đệ quy; tạo trong begin()
bool gInited = false;
bool gBeginResult = false;
bool gChipPresent = false;
bool gTrusted = false;                        // giờ trong chip đáng tin
bool gCacheValid = false;
DateTime gCache{};
uint32_t gCacheAt = 0;

struct Lock {
  bool ok;
  Lock() : ok(gMutex != nullptr &&
              xSemaphoreTakeRecursive(gMutex, pdMS_TO_TICKS(kLockWaitMs)) == pdTRUE) {}
  ~Lock() { if (ok) xSemaphoreGiveRecursive(gMutex); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

// ---------- tính toán thuần ----------

bool bcdOk(uint8_t v) { return (v & 0x0F) <= 9 && (v >> 4) <= 9; }
uint8_t bcd2bin(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t bin2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

bool isLeap(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

uint8_t daysInMonth(uint16_t y, uint8_t m) {
  static const uint8_t d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  return static_cast<uint8_t>(m == 2 && isLeap(y) ? 29 : d[m - 1]);
}

// 0 = Chủ Nhật (Sakamoto)
uint8_t weekdayOf(uint16_t y, uint8_t m, uint8_t d) {
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int yy = y;
  if (m < 3) yy -= 1;
  return static_cast<uint8_t>((yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7);
}

bool valid(const DateTime& d) {
  return d.year >= 2000 && d.year <= 2099 && d.month >= 1 && d.month <= 12 && d.day >= 1 &&
         d.day <= daysInMonth(d.year, d.month) && d.hour < 24 && d.minute < 60 && d.second < 60;
}

// 7 byte thanh ghi -> DateTime. false nếu BCD hỏng hoặc giá trị ngoài khoảng.
bool decodeTime(const uint8_t r[7], DateTime* out) {
  uint8_t sec = r[0] & 0x7F, min = r[1] & 0x7F, day = r[4] & 0x3F, mon = r[5] & 0x1F, yr = r[6];
  if (!bcdOk(sec) || !bcdOk(min) || !bcdOk(day) || !bcdOk(mon) || !bcdOk(yr)) return false;

  uint8_t hr;
  if (r[2] & 0x40) {                          // chế độ 12 giờ (chip khác đã ghi); đổi sang 24
    uint8_t h12 = r[2] & 0x1F;
    if (!bcdOk(h12)) return false;
    h12 = bcd2bin(h12);
    if (h12 < 1 || h12 > 12) return false;
    hr = static_cast<uint8_t>((h12 % 12) + ((r[2] & 0x20) ? 12 : 0));
  } else {
    uint8_t h24 = r[2] & 0x3F;
    if (!bcdOk(h24)) return false;
    hr = bcd2bin(h24);
  }

  DateTime d{};
  d.year = static_cast<uint16_t>(2000 + bcd2bin(yr) + ((r[5] & 0x80) ? 100 : 0));  // bit thế kỷ: ngoài 2000-2099 thì invalid
  d.month = bcd2bin(mon);
  d.day = bcd2bin(day);
  d.hour = hr;
  d.minute = bcd2bin(min);
  d.second = bcd2bin(sec);
  if (!valid(d)) return false;
  d.weekday = weekdayOf(d.year, d.month, d.day);   // không tin thanh ghi thứ
  *out = d;
  return true;
}

void encodeTime(const DateTime& d, uint8_t r[7]) {
  r[0] = bin2bcd(d.second);
  r[1] = bin2bcd(d.minute);
  r[2] = bin2bcd(d.hour);                      // bit 6 = 0: chế độ 24 giờ
  r[3] = static_cast<uint8_t>(d.weekday + 1);  // DS3231: 1..7
  r[4] = bin2bcd(d.day);
  r[5] = bin2bcd(d.month);                     // bit thế kỷ = 0
  r[6] = bin2bcd(static_cast<uint8_t>(d.year - 2000));
}

// ---------- I2C (gọi khi đang giữ Lock) ----------

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(kAddr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

bool writeRegs(uint8_t reg, const uint8_t* data, uint8_t n) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(data, n);
  return Wire.endTransmission(true) == 0;
}

// ---------- giờ hệ thống ----------

DateTime systemNow() {
  time_t t = time(nullptr);
  tm lt{};
  localtime_r(&t, &lt);
  DateTime d{};
  d.year = static_cast<uint16_t>(lt.tm_year + 1900);
  d.month = static_cast<uint8_t>(lt.tm_mon + 1);
  d.day = static_cast<uint8_t>(lt.tm_mday);
  d.hour = static_cast<uint8_t>(lt.tm_hour);
  d.minute = static_cast<uint8_t>(lt.tm_min);
  d.second = static_cast<uint8_t>(lt.tm_sec);
  d.weekday = static_cast<uint8_t>(lt.tm_wday);
  return d;
}

void applySystemTime(const DateTime& d) {
  tm t{};
  t.tm_year = d.year - 1900;
  t.tm_mon = d.month - 1;
  t.tm_mday = d.day;
  t.tm_hour = d.hour;
  t.tm_min = d.minute;
  t.tm_sec = d.second;
  t.tm_isdst = -1;
  timeval tv{};
  tv.tv_sec = mktime(&t);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

}  // namespace

namespace TimeService {

bool begin() {
  if (gInited) return gBeginResult;

  gMutex = xSemaphoreCreateRecursiveMutex();
  setenv("TZ", kDefaultTz, 1);
  tzset();

  Wire.begin(static_cast<int>(Pins::I2cSda), static_cast<int>(Pins::I2cScl), kI2cHz);
  Wire.setTimeOut(kI2cTimeoutMs);

  {
    Lock l;
    Wire.beginTransmission(kAddr);
    gChipPresent = (Wire.endTransmission(true) == 0);

    if (!gChipPresent) {
      Serial.println("[Time] khong thay DS3231, dung gio he thong");
    } else {
      uint8_t st = 0, r[7];
      DateTime dt{};
      if (!readRegs(kRegStatus, &st, 1)) {
        Serial.println("[Time] doc status DS3231 loi, dung gio he thong");
      } else if (st & kStatusOsf) {
        Serial.println("[Time] DS3231 mat nguon (OSF), gio chua dang tin, cho set()/NTP");
      } else if (!readRegs(kRegTime, r, 7) || !decodeTime(r, &dt)) {
        Serial.println("[Time] gio trong DS3231 khong hop le, dung gio he thong");
      } else {
        gTrusted = true;
        applySystemTime(dt);
        Serial.printf("[Time] DS3231 OK: %04u-%02u-%02u %02u:%02u:%02u\n", dt.year, dt.month,
                      dt.day, dt.hour, dt.minute, dt.second);
      }
    }
  }

  gBeginResult = gChipPresent && gTrusted;
  gInited = true;
  return gBeginResult;
}

DateTime now() {
  Lock l;
  if (!l.ok || !gChipPresent || !gTrusted) return systemNow();

  uint32_t ms = millis();
  if (gCacheValid && (ms - gCacheAt) < kCacheMs) return gCache;

  uint8_t r[7];
  DateTime dt{};
  if (readRegs(kRegTime, r, 7) && decodeTime(r, &dt)) {
    gCache = dt;
    gCacheAt = ms;
    gCacheValid = true;
    return dt;
  }
  return systemNow();
}

void set(const DateTime& in) {
  if (!valid(in)) {
    Serial.println("[Time] set(): gio khong hop le, bo qua");
    return;
  }
  DateTime dt = in;
  dt.weekday = weekdayOf(dt.year, dt.month, dt.day);

  Lock l;
  applySystemTime(dt);
  if (!l.ok || !gInited || !gChipPresent) return;

  gCacheValid = false;
  uint8_t r[7], st = 0;
  encodeTime(dt, r);
  bool ok = writeRegs(kRegTime, r, 7) && readRegs(kRegStatus, &st, 1);
  if (ok) {
    st = static_cast<uint8_t>(st & ~kStatusOsf);
    ok = writeRegs(kRegStatus, &st, 1);
  }
  gTrusted = ok;   // ghi lỗi thì now() quay về giờ hệ thống (đã được đặt đúng ở trên)
  if (!ok) Serial.println("[Time] ghi DS3231 loi, tam dung gio he thong");
}

bool syncNtp(const char* tz) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Time] NTP: chua co WiFi");
    return false;
  }
  const char* zone = (tz != nullptr && *tz != '\0') ? tz : kDefaultTz;

  // Phải reset cờ: giờ hệ thống đã được nạp từ RTC nên getLocalTime() luôn "thành công".
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(zone, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  bool ok = false;
  uint32_t start = millis();
  while (millis() - start < kNtpTimeoutMs) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      ok = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  sntp_stop();   // DS3231 là nguồn giờ chính, không để SNTP tự chỉnh giờ hệ thống ngầm

  if (!ok) {
    Serial.println("[Time] NTP: het thoi gian cho");
    return false;
  }
  set(systemNow());   // giờ địa phương theo `zone`, ghi vào DS3231 nếu có
  Serial.println("[Time] NTP: dong bo xong");
  return true;
}

}  // namespace TimeService