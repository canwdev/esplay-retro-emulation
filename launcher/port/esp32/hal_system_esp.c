#include "hal_system.h"

#include "esp_app_desc.h"
#include "esp_system.h"

const char *hal_system_app_version(void) {
  const esp_app_desc_t *desc = esp_app_get_description();
  return desc ? desc->version : "";
}

void hal_system_reboot(void) {
  esp_restart();
}
