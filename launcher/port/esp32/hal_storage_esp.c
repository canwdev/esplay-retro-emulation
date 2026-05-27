#include "hal_storage.h"

#include "sdcard.h"

const char *hal_storage_root(void) {
  return "/sd";
}

bool hal_storage_mount(void) {
  return sdcard_open(hal_storage_root()) == ESP_OK;
}

void hal_storage_get_free_kb(uint32_t *total_kb, uint32_t *free_kb) {
  if (!total_kb || !free_kb)
    return;
  sdcard_get_free_space(total_kb, free_kb);
}
