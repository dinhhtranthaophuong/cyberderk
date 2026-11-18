#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#ifdef __cplusplus
extern "C" {
#endif
void* heap_caps_malloc(size_t size, unsigned caps);
void* heap_caps_realloc(void* p, size_t size, unsigned caps);
void heap_caps_free(void* p);
size_t heap_caps_get_free_size(unsigned caps);
#ifdef __cplusplus
}
#endif
