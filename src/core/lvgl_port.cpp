#include "lvgl_port.hpp"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "config/display.hpp"
#include "config/pins.hpp"
#include "core/psram.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

TFT_eSPI tft;
lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;
SemaphoreHandle_t lvglMutex = nullptr;

void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors(reinterpret_cast<uint16_t*>(&px->full), w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

void logCb(const char* msg) {
  Serial.print(msg);
}

// Task LVGL: chay lv_timer_handler() deu dan tren Core 1.
void lvglTask(void*) {
  for (;;) {
    LvglPort::tick();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

namespace LvglPort {

bool init() {
  lvglMutex = xSemaphoreCreateRecursiveMutex();

  // Den nen: neu chan LED cua man noi thang 3V3 thi dong nay vo hai.
  pinMode(Pins::TftBl, OUTPUT);
  digitalWrite(Pins::TftBl, HIGH);

  tft.init();
  tft.invertDisplay(false);  // ST7789: false hoac true tuy module. Neu bi am ban thi doi gia tri
  tft.setRotation(1);  // ngang: 160x128
  tft.fillScreen(TFT_BLACK);

  lv_init();
  lv_log_register_print_cb(logCb);

  // Hai buffer ve, moi buffer = 1/10 man hinh, dat trong PSRAM.
  const size_t pixels = static_cast<size_t>(UI_W) * LVGL_BUF_LINES;
  auto* buf1 = static_cast<lv_color_t*>(Psram::alloc(pixels * sizeof(lv_color_t)));
  auto* buf2 = static_cast<lv_color_t*>(Psram::alloc(pixels * sizeof(lv_color_t)));
  if (!buf1 || !buf2) {
    Serial.println("LvglPort: khong cap phat duoc buffer trong PSRAM");
    Psram::release(buf1);
    Psram::release(buf2);
    return false;
  }
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, pixels);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = UI_W;
  dispDrv.ver_res = UI_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  // Stack 12 KB, uu tien 2 (cao hon loop() la 1), ghim vao Core 1.
  xTaskCreatePinnedToCore(lvglTask, "lvgl", 12 * 1024, nullptr, 2, nullptr, 1);
  return true;
}

void tick() {
  LvglLock lock;
  lv_timer_handler();
}

}  // namespace LvglPort

LvglLock::LvglLock() {
  if (lvglMutex) xSemaphoreTakeRecursive(lvglMutex, portMAX_DELAY);
}

LvglLock::~LvglLock() {
  if (lvglMutex) xSemaphoreGiveRecursive(lvglMutex);
}
