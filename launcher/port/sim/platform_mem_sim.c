#include "platform_mem.h"

#include <stdlib.h>

/* Approximate ESP32 heap after file manager frees s_entries. The streaming
 * text reader no longer caps file size from this value, but keeping it low
 * helps simulator logs and allocation behavior stay close to hardware. */
#define SIM_FREE_HEAP_BYTES 88000u

void *platform_malloc(size_t size) {
  return malloc(size);
}

void platform_free(void *ptr) {
  free(ptr);
}

uint32_t platform_free_heap(void) {
  return SIM_FREE_HEAP_BYTES;
}

uint32_t platform_largest_free_block(void) {
  return SIM_FREE_HEAP_BYTES;
}

uint32_t platform_flash_size(void) {
  return 16u * 1024 * 1024;
}

uint32_t platform_psram_size(void) {
  return 0;
}