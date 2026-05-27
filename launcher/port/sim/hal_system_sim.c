#include "hal_system.h"

#include "platform_log.h"

#include <stdlib.h>

const char *hal_system_app_version(void) {
  return "sim";
}

void hal_system_reboot(void) {
  platform_log(PLATFORM_LOG_INFO, "hal_system", "reboot requested (exit)");
  exit(0);
}
