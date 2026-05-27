#include "hal_power.h"

void hal_power_init(void) {
}

bool hal_power_read_battery(hal_battery_t *out) {
  if (!out)
    return false;
  out->millivolts = 4200;
  out->percentage = 85;
  out->charging = 0;
  return true;
}
