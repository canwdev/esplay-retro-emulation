#pragma once

#include <stddef.h>
#include <stdint.h>

void *platform_malloc(size_t size);
void platform_free(void *ptr);
uint32_t platform_free_heap(void);
uint32_t platform_largest_free_block(void);
uint32_t platform_flash_size(void);
uint32_t platform_psram_size(void);
