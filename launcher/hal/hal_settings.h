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
  SettingMax,
} Setting;
#endif

int hal_settings_load(Setting id, int32_t *v);
int hal_settings_save(Setting id, int32_t v);
