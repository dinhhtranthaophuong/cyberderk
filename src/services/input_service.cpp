#include "services/input_service.hpp"

#include <Arduino.h>

#include "config/keymap.hpp"
#include "core/app_manager.hpp"
#include "services/keypad_matrix.hpp"

namespace {

// Hang doi phim cho lv_indev. Chi duoc dung trong read_cb (task LVGL) nen khong can khoa.
constexpr size_t kQueueSize = 16;
uint32_t queue[kQueueSize];
size_t qHead = 0;
size_t qTail = 0;

bool pressedNow = false;
uint32_t lastKey = 0;

lv_indev_drv_t indevDrv;
lv_indev_t* indevPtr = nullptr;
lv_group_t* groupPtr = nullptr;

void push(uint32_t key) {
  const size_t next = (qTail + 1) % kQueueSize;
  if (next == qHead) return;  // day: bo phim moi
  queue[qTail] = key;
  qTail = next;
}

// Ky tu go tren Serial Monitor -> ma phim. Tra 0 neu khong phai phim.
uint32_t mapChar(int c) {
  if (c >= '1' && c <= '9') return keyForNumber(c - '0');
  if (c == 'h' || c == 'H') return KEY_HOME;  // gia lap giu phim 1
  return 0;
}

// Phim custom (HOME, OPTION) di qua AppManager, con lai vao hang doi cua lv_indev.
void route(uint32_t key) {
  if (key == KEY_HOME || key == KEY_OPTION) {
    AppManager::postKey(key);
  } else {
    push(key);
  }
}

// Doc het ky tu dang cho tren Serial (tien cho thu nghiem, luon bat).
void drainSerial() {
  while (Serial.available() > 0) {
    const uint32_t key = mapChar(Serial.read());
    if (key != 0) route(key);
  }
}

// Ban phim ma tran that. Luc ranh chi ton mot lan doc bien.
void drainKeypad() {
  const uint32_t key = KeypadMatrix::poll(millis());
  if (key == 0) return;
#if defined(KEYPAD_DEBUG) && KEYPAD_DEBUG
  Serial.printf("[Keypad] key 0x%04X\n", (unsigned)key);
#endif
  route(key);
}

// Moi phim = mot lan PRESSED roi mot lan RELEASED.
void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  drainSerial();
  drainKeypad();

  if (pressedNow) {
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = lastKey;
    pressedNow = false;
    data->continue_reading = (qHead != qTail);
    return;
  }
  if (qHead != qTail) {
    lastKey = queue[qHead];
    qHead = (qHead + 1) % kQueueSize;
    pressedNow = true;
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = lastKey;
    data->continue_reading = true;  // doc tiep de nhan RELEASED ngay
    return;
  }
  data->state = LV_INDEV_STATE_RELEASED;
  data->key = lastKey;
  data->continue_reading = false;
}

}  // namespace

namespace InputService {

void init() {
  groupPtr = lv_group_create();
  lv_group_set_default(groupPtr);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_KEYPAD;
  indevDrv.read_cb = readCb;
  indevPtr = lv_indev_drv_register(&indevDrv);
  lv_indev_set_group(indevPtr, groupPtr);

  KeypadMatrix::init();
}

lv_indev_t* indev() { return indevPtr; }
lv_group_t* defaultGroup() { return groupPtr; }

}  // namespace InputService
