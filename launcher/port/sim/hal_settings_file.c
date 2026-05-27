#include "hal_settings.h"

#include "platform_log.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "hal_settings";
static const char *s_path = "launcher_sim_settings.ini";

static bool parse_line(const char *line, int *out_id, int32_t *out_val) {
  if (!line || !out_id || !out_val)
    return false;
  int id = -1;
  long v = 0;
  if (sscanf(line, "%d=%ld", &id, &v) != 2)
    return false;
  if (id < 0 || id >= (int)SettingMax)
    return false;
  *out_id = id;
  *out_val = (int32_t)v;
  return true;
}

int hal_settings_load(Setting id, int32_t *v) {
  if (!v || id < 0 || id >= SettingMax)
    return -1;

  FILE *f = fopen(s_path, "rb");
  if (!f)
    return -1;

  char line[128];
  while (fgets(line, sizeof(line), f)) {
    int k = -1;
    int32_t val = 0;
    if (!parse_line(line, &k, &val))
      continue;
    if (k == (int)id) {
      fclose(f);
      *v = val;
      return 0;
    }
  }

  fclose(f);
  return -1;
}

int hal_settings_save(Setting id, int32_t v) {
  if (id < 0 || id >= SettingMax)
    return -1;

  int32_t values[SettingMax];
  bool has[SettingMax];
  memset(values, 0, sizeof(values));
  memset(has, 0, sizeof(has));

  FILE *f = fopen(s_path, "rb");
  if (f) {
    char line[128];
    while (fgets(line, sizeof(line), f)) {
      int k = -1;
      int32_t val = 0;
      if (!parse_line(line, &k, &val))
        continue;
      values[k] = val;
      has[k] = true;
    }
    fclose(f);
  }

  values[id] = v;
  has[id] = true;

  f = fopen(s_path, "wb");
  if (!f) {
    platform_log(PLATFORM_LOG_WARN, TAG, "open failed: %s", s_path);
    return -1;
  }

  for (int i = 0; i < (int)SettingMax; i++) {
    if (!has[i])
      continue;
    fprintf(f, "%d=%ld\n", i, (long)values[i]);
  }
  fclose(f);
  return 0;
}
