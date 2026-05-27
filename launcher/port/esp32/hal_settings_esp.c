#include "hal_settings.h"

int hal_settings_load(Setting id, int32_t *v) {
  return settings_load(id, v);
}

int hal_settings_save(Setting id, int32_t v) {
  return settings_save(id, v);
}
