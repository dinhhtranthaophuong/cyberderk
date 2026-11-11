// Chat 0 - Buoc 3: AppManager + Main Screen (launcher tam) + cac app stub.
// Phim qua Serial Monitor (go 1-9, h). Chua co bang phim 3x3, SD, loa, RTC.
//
// Luu y: task LVGL chay rieng tren Core 1 (LvglPort). loop() chi goi AppManager::tick().

#include <Arduino.h>
#include <lvgl.h>

#include "config/display.hpp"
#include "config/keymap.hpp"
#include "config/pins.hpp"
#include "core/app_manager.hpp"
#include "core/lvgl_port.hpp"
#include "core/psram.hpp"
#include "services/input_service.hpp"
#include "services/settings_store.hpp"
#include "services/sd_storage.hpp"

namespace {
uint32_t lastReportMs = 0;
}

void setup() {
  Serial.begin(115200);
  // Cho cong USB CDC toi da 3 giay de khong mat log dau (khong dung delay()).
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
    yield();
  }
  Serial.println();
  Serial.println("=== Cyberdeck chat 0 / buoc 3: AppManager ===");
  Serial.printf("Flash: %u KB, PSRAM free: %u KB\n", (unsigned)(ESP.getFlashChipSize() / 1024),
                (unsigned)(Psram::freeBytes() / 1024));

  if (SdStorage::begin()) {
    Serial.printf("[SD] %u anh, %u bai nhac\n",
                  (unsigned)SdStorage::listFiles("/images", ".jpg;.jpeg").size(),
                  (unsigned)SdStorage::listFiles("/music", ".mp3").size());
  }
  
  if (!LvglPort::init()) {
    Serial.println("LvglPort::init() THAT BAI");
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  {
    // Task LVGL da chay, nen khoi tao UI phai giu khoa.
    LvglLock lock;
    SettingsStore::begin();
    InputService::init();
    AppManager::init();
  }

  Serial.println("San sang. Go trong Serial Monitor:");
  Serial.println("  2/8/4/6 = len/xuong/trai/phai, 5 = ENTER, 7/9 = PREV/NEXT");
  Serial.println("  1 = BACK (ESC), 3 = OPTION, h = HOME");
}

void loop() {
  const uint32_t now = millis();
  AppManager::tick(now);

  if (now - lastReportMs >= 5000) {
    lastReportMs = now;
    Serial.printf("[%lu s] heap %u KB, psram %u KB\n", (unsigned long)(now / 1000),
                  (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(Psram::freeBytes() / 1024));
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}