#include "hal_settings.h"

int hal_settings_load(Setting id, int32_t *v) {
  return settings_load(id, v);
}

int hal_settings_save(Setting id, int32_t v) {
  return settings_save(id, v);
}

char *hal_settings_load_str(Setting id) {
  return settings_load_str(id);
}

int hal_settings_save_str(Setting id, const char *value) {
  return settings_save_str(id, value);
}
