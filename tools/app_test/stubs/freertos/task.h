#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* h, BaseType_t core);
void vTaskDelete(TaskHandle_t h);   // nullptr = tu xoa, khong bao gio quay lai
