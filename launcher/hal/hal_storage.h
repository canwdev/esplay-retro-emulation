#pragma once

#include <stdbool.h>
#include <stdint.h>

const char *hal_storage_root(void);
bool hal_storage_mount(void);
void hal_storage_get_free_kb(uint32_t *total_kb, uint32_t *free_kb);
