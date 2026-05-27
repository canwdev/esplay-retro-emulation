#include "platform_mem.h"

#include <stdlib.h>

void *platform_malloc(size_t size) {
  return malloc(size);
}

void platform_free(void *ptr) {
  free(ptr);
}

uint32_t platform_free_heap(void) {
  return 200000;
}

uint32_t platform_largest_free_block(void) {
  return 200000;
}
