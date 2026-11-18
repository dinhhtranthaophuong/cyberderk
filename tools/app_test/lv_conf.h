// Cau hinh LVGL 8.3 toi thieu. Moi tuy chon khong khai bao se lay mac dinh
// trong lv_conf_internal.h cua LVGL.
#if 1 /* Dat 1 de bat noi dung file nay */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Man hinh RGB565. Khong swap byte o day: lvgl_port dung pushColors(..., swap=true). */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Heap cua LVGL nam trong PSRAM (8 MB), khong bi gioi han 48 KB nhu vung tinh trong RAM noi. */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 1
#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE heap_caps_free
#define LV_MEM_CUSTOM_REALLOC(p, size) heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif

/* Dong ho cho LVGL lay tu millis(), khong can goi lv_tick_inc(). */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 10

/* Log cua LVGL: chi canh bao tro len. lvgl_port dang ky callback in ra Serial. */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0

/* Font: ASCII (khong co dau tieng Viet). Se bo sung font rieng khi lam UI that. */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_12

#endif /* LV_CONF_H */

#endif /* Het phan bat/tat */
