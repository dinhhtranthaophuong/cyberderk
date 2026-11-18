#pragma once
#include "FreeRTOS.h"
typedef void* QueueHandle_t;
QueueHandle_t xQueueCreate(uint32_t len, uint32_t itemSize);
BaseType_t xQueueSendToBack(QueueHandle_t, const void* item, uint32_t ticks);
BaseType_t xQueueSendToFront(QueueHandle_t, const void* item, uint32_t ticks);
#define xQueueSend xQueueSendToBack
BaseType_t xQueueReceive(QueueHandle_t, void* item, uint32_t ticks);
void vQueueDelete(QueueHandle_t);
