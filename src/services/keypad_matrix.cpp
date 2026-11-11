#include "services/keypad_matrix.hpp"

#include <Arduino.h>

#include "config/keymap.hpp"
#include "config/pins.hpp"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace {

constexpr int kN = 3;
constexpr uint32_t kScanMs = 5;      // chu ky quet khi co phim
constexpr uint8_t kStableScans = 2;  // so lan quet giong nhau lien tiep de coi la on dinh (~10-15 ms)
constexpr uint8_t kIdleScans = 4;    // so lan quet "khong phim" lien tiep truoc khi ve trang thai ranh
constexpr uint8_t kMulti = 0xFF;     // ket qua quet: nhieu phim

enum class Mode : uint8_t { Idle, Active };

volatile bool wake = false;
Mode mode = Mode::Idle;
uint32_t lastScan = 0;

uint8_t candidate = 0;   // ket qua quet gan nhat
uint8_t candCount = 0;   // so lan quet lien tiep ra cung ket qua
uint8_t stable = 0;      // trang thai da chong doi: 0 = khong phim, 1..9 = phim, kMulti
uint8_t quietScans = 0;
bool locked = false;     // true: bo qua phim cho toi khi tha het (luc khoi dong / sau khi nhan nhieu phim)

bool keyOneDown = false;  // phim 1 dang cho phan biet nhan nhanh / giu
bool homeFired = false;
uint32_t keyOneSince = 0;

void IRAM_ATTR onColFall() { wake = true; }

bool anyColLow() {
  for (int c = 0; c < kN; ++c) {
    if (digitalRead(Pins::KeyCol[c]) == LOW) return true;
  }
  return false;
}

// Trang thai ranh: moi hang LOW, cot pull-up, bat ngat. Bat ngat TRUOC khi kiem tra muc cot de
// khong lo phim nhan giua chung.
void arm() {
  for (int r = 0; r < kN; ++r) {
    pinMode(Pins::KeyRow[r], OUTPUT);
    digitalWrite(Pins::KeyRow[r], LOW);
  }
  for (int c = 0; c < kN; ++c) pinMode(Pins::KeyCol[c], INPUT_PULLUP);
  delayMicroseconds(5);
  wake = false;
  for (int c = 0; c < kN; ++c) {
    attachInterrupt(digitalPinToInterrupt(Pins::KeyCol[c]), onColFall, FALLING);
  }
  if (anyColLow()) wake = true;  // phim da nam san (hoac nhan ngay luc bat ngat)
  mode = Mode::Idle;
}

void enterActive(uint32_t now) {
  for (int c = 0; c < kN; ++c) detachInterrupt(digitalPinToInterrupt(Pins::KeyCol[c]));
  for (int r = 0; r < kN; ++r) pinMode(Pins::KeyRow[r], INPUT);  // high-Z
  mode = Mode::Active;
  candidate = 0;
  candCount = 0;
  stable = 0;
  quietScans = 0;
  keyOneDown = false;
  homeFired = false;
  lastScan = now - kScanMs;  // quet ngay lan dau
}

// Quet tung hang: chi mot hang OUTPUT LOW, hai hang con lai high-Z.
uint8_t scanRaw() {
  uint8_t count = 0;
  uint8_t key = 0;
  for (int r = 0; r < kN; ++r) {
    pinMode(Pins::KeyRow[r], OUTPUT);
    digitalWrite(Pins::KeyRow[r], LOW);
    delayMicroseconds(3);
    for (int c = 0; c < kN; ++c) {
      if (digitalRead(Pins::KeyCol[c]) == LOW) {
        ++count;
        key = static_cast<uint8_t>(r * kN + c + 1);
      }
    }
    pinMode(Pins::KeyRow[r], INPUT);
  }
  if (count == 0) return 0;
  return count == 1 ? key : kMulti;
}

// Trang thai on dinh vua doi tu `prev` sang `now`. Tra ma phim neu co su kien.
uint32_t onStableChange(uint8_t prev, uint8_t next, uint32_t nowMs) {
  uint32_t ev = 0;

  // Phim 1 vua duoc tha. Neu chua giu du lau va khong bi cat boi phim khac thi la ESC.
  if (prev == 1 && keyOneDown) {
    keyOneDown = false;
    if (next == 0 && !homeFired) ev = keyForNumber(1);
  }

  if (next == 0) {
    locked = false;
  } else if (next == kMulti) {
    locked = true;
  } else if (!locked) {
    if (next == 1) {
      keyOneDown = true;
      homeFired = false;
      keyOneSince = nowMs;
    } else {
      ev = keyForNumber(next);
    }
  }
  return ev;
}

}  // namespace

namespace KeypadMatrix {

void init() {
  arm();
  // Phim dang bi giu luc bat nguon: bo qua cho toi khi tha.
  locked = wake;
}

bool isIdle() { return mode == Mode::Idle; }

uint32_t poll(uint32_t nowMs) {
  if (mode == Mode::Idle) {
    if (!wake) return 0;  // duong ranh: chi doc mot bien
    enterActive(nowMs);
  }

  if (nowMs - lastScan < kScanMs) return 0;
  lastScan = nowMs;

  const uint8_t raw = scanRaw();
  if (raw == candidate) {
    if (candCount < 255) ++candCount;
  } else {
    candidate = raw;
    candCount = 1;
  }

  uint32_t ev = 0;
  if (candCount >= kStableScans && candidate != stable) {
    const uint8_t prev = stable;
    stable = candidate;
    ev = onStableChange(prev, stable, nowMs);
  }

  // Giu phim 1 du lau -> HOME (mot lan), ESC se khong gui khi tha.
  if (ev == 0 && stable == 1 && keyOneDown && !homeFired &&
      (nowMs - keyOneSince) >= KEY_HOME_HOLD_MS) {
    homeFired = true;
    ev = KEY_HOME;
  }

  // Ve trang thai ranh khi da tha het phim mot luc.
  if (raw == 0 && stable == 0) {
    if (++quietScans >= kIdleScans && ev == 0) arm();
  } else {
    quietScans = 0;
  }
  return ev;
}

}  // namespace KeypadMatrix
