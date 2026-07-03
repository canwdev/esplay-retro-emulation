#pragma once

#include <stdint.h>

#ifdef TARGET_ESP32
#include "settings.h"
#else
typedef enum Setting {
  SettingAudioVolume = 0,
  SettingBacklight,
  SettingPlayingMode,
  SettingRomPath,
  SettingScaleMode,
  SettingWifi,
  SettingAlg,
  SettingUiTheme,
  SettingBacklightTimeout,
  SettingMusicSessionArmed,
  SettingMusicSessionPath,
  SettingEqPreset,
  SettingMax,
} Setting;
#endif

int hal_settings_load(Setting id, int32_t *v);
int hal_settings_save(Setting id, int32_t v);
char *hal_settings_load_str(Setting id);
int hal_settings_save_str(Setting id, const char *value);
