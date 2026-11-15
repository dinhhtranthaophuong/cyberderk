#pragma once
#include "freertos/FreeRTOS.h"
struct SemStub;
typedef SemStub* SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t h);
// Chi dung trong stub: tao khoa co hang (rank) de kiem tra thu tu khoa.
SemaphoreHandle_t stubMutexCreate(int rank, bool recursive, const char* name);
