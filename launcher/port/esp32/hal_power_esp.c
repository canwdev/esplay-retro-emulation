#include "hal_power.h"

#include "power.h"

void hal_power_init(void) {
  battery_level_init();
}

bool hal_power_read_battery(hal_battery_t *out) {
  if (!out)
    return false;

  battery_state s;
  battery_level_read(&s);

  out->millivolts = s.millivolts;
  out->percentage = s.percentage;
  out->charging = (s.state == CHARGING || s.state == FULL_CHARGED) ? 1 : 0;
  return true;
}
