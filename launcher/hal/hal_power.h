#pragma once

#include <stdbool.h>

typedef struct {
  int millivolts;
  int percentage;
  int charging;
} hal_battery_t;

void hal_power_init(void);
bool hal_power_read_battery(hal_battery_t *out);
