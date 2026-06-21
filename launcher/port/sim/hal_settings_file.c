#include "hal_settings.h"

#include "platform_log.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "hal_settings";
static const char *s_path = "launcher_sim_settings.ini";

static char *dup_string(const char *src) {
  if (!src)
    return NULL;
  size_t len = strlen(src) + 1;
  char *copy = (char *)malloc(len);
  if (!copy)
    return NULL;
  memcpy(copy, src, len);
  return copy;
}

static bool parse_int_line(const char *line, int *out_id, int32_t *out_val) {
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

static bool parse_str_line(const char *line, int *out_id, const char **out_val) {
  if (!line || !out_id || !out_val)
    return false;
  if (line[0] != 'S')
    return false;
  const char *eq = strchr(line, '=');
  if (!eq || eq <= line + 1)
    return false;
  int id = atoi(line + 1);
  if (id < 0 || id >= (int)SettingMax)
    return false;
  const char *value = eq + 1;
  size_t len = strlen(value);
  while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r'))
    len--;
  static char buf[512];
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  memcpy(buf, value, len);
  buf[len] = '\0';
  *out_id = id;
  *out_val = buf;
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
    if (!parse_int_line(line, &k, &val))
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
  char *strings[SettingMax];
  memset(values, 0, sizeof(values));
  memset(has, 0, sizeof(has));
  memset(strings, 0, sizeof(strings));

  FILE *f = fopen(s_path, "rb");
  if (f) {
    char line[640];
    while (fgets(line, sizeof(line), f)) {
      int k = -1;
      int32_t val = 0;
      const char *str = NULL;
      if (parse_int_line(line, &k, &val)) {
        values[k] = val;
        has[k] = true;
        continue;
      }
      if (parse_str_line(line, &k, &str)) {
        free(strings[k]);
        strings[k] = dup_string(str);
      }
    }
    fclose(f);
  }

  values[id] = v;
  has[id] = true;

  f = fopen(s_path, "wb");
  if (!f) {
    platform_log(PLATFORM_LOG_WARN, TAG, "open failed: %s", s_path);
    for (int i = 0; i < (int)SettingMax; i++)
      free(strings[i]);
    return -1;
  }

  for (int i = 0; i < (int)SettingMax; i++) {
    if (!has[i])
      continue;
    fprintf(f, "%d=%ld\n", i, (long)values[i]);
  }
  for (int i = 0; i < (int)SettingMax; i++) {
    if (strings[i])
      fprintf(f, "S%d=%s\n", i, strings[i]);
  }
  fclose(f);
  for (int i = 0; i < (int)SettingMax; i++)
    free(strings[i]);
  return 0;
}

char *hal_settings_load_str(Setting id) {
  if (id < 0 || id >= SettingMax)
    return NULL;

  FILE *f = fopen(s_path, "rb");
  if (!f)
    return NULL;

  char line[640];
  while (fgets(line, sizeof(line), f)) {
    int k = -1;
    const char *val = NULL;
    if (!parse_str_line(line, &k, &val))
      continue;
    if (k == (int)id) {
      fclose(f);
      size_t len = strlen(val) + 1;
      char *copy = (char *)malloc(len);
      if (!copy)
        return NULL;
      memcpy(copy, val, len);
      return copy;
    }
  }

  fclose(f);
  return NULL;
}

int hal_settings_save_str(Setting id, const char *value) {
  if (id < 0 || id >= SettingMax || !value)
    return -1;

  int32_t values[SettingMax];
  bool has[SettingMax];
  char *strings[SettingMax];
  memset(values, 0, sizeof(values));
  memset(has, 0, sizeof(has));
  memset(strings, 0, sizeof(strings));

  FILE *f = fopen(s_path, "rb");
  if (f) {
    char line[640];
    while (fgets(line, sizeof(line), f)) {
      int k = -1;
      int32_t val = 0;
      const char *str = NULL;
      if (parse_int_line(line, &k, &val)) {
        values[k] = val;
        has[k] = true;
        continue;
      }
      if (parse_str_line(line, &k, &str)) {
        free(strings[k]);
        strings[k] = dup_string(str);
      }
    }
    fclose(f);
  }

  free(strings[id]);
  strings[id] = dup_string(value);

  f = fopen(s_path, "wb");
  if (!f) {
    platform_log(PLATFORM_LOG_WARN, TAG, "open failed: %s", s_path);
    for (int i = 0; i < (int)SettingMax; i++)
      free(strings[i]);
    return -1;
  }

  for (int i = 0; i < (int)SettingMax; i++) {
    if (has[i])
      fprintf(f, "%d=%ld\n", i, (long)values[i]);
  }
  for (int i = 0; i < (int)SettingMax; i++) {
    if (strings[i])
      fprintf(f, "S%d=%s\n", i, strings[i]);
  }
  fclose(f);

  for (int i = 0; i < (int)SettingMax; i++)
    free(strings[i]);
  return 0;
}
