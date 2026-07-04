#include "platform_mem.h"

#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#include <stdlib.h>

void *platform_malloc(size_t size) {
  return malloc(size);
}

void platform_free(void *ptr) {
  free(ptr);
}

uint32_t platform_free_heap(void) {
  return (uint32_t)esp_get_free_heap_size();
}

uint32_t platform_largest_free_block(void) {
  return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

uint32_t platform_flash_size(void) {
  uint32_t size = 0;
  esp_flash_get_size(NULL, &size);
  return size;
}

uint32_t platform_psram_size(void) {
  return (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}
