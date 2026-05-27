#include "ui_backlight.h"
#include "hal_display.h"
#include "hal_settings.h"
#include "platform_log.h"
#include "lvgl.h"

static const char *TAG = "ui_backlight";

#define BACKLIGHT_TIMEOUT_DEFAULT 30000

static bool s_backlight_on = true;
static uint32_t s_last_activity_ms = 0;
static uint8_t s_restore_brightness = 70;
static int32_t s_timeout_ms = BACKLIGHT_TIMEOUT_DEFAULT;

void ui_backlight_init(void) {
  int32_t saved_bl = 70;
  if (hal_settings_load(SettingBacklight, &saved_bl) == 0) {
    if (saved_bl < 10) saved_bl = 10;
    if (saved_bl > 100) saved_bl = 100;
    s_restore_brightness = (uint8_t)saved_bl;
  }

  int32_t saved_timeout = 30; // default 30s
  if (hal_settings_load(SettingBacklightTimeout, &saved_timeout) == 0) {
      s_timeout_ms = saved_timeout * 1000;
  } else {
      s_timeout_ms = BACKLIGHT_TIMEOUT_DEFAULT;
  }

  s_backlight_on = true;
  s_last_activity_ms = lv_tick_get();
  hal_display_set_brightness(s_restore_brightness);
}

static void update_restore_brightness(void) {
  int32_t saved_bl = s_restore_brightness;
  if (hal_settings_load(SettingBacklight, &saved_bl) == 0) {
    if (saved_bl >= 10 && saved_bl <= 100)
      s_restore_brightness = (uint8_t)saved_bl;
  }

  int32_t saved_timeout = 30;
  if (hal_settings_load(SettingBacklightTimeout, &saved_timeout) == 0) {
      s_timeout_ms = saved_timeout * 1000;
  }
}

void ui_backlight_set_on(bool on) {
  if (s_backlight_on == on) {
    if (on) s_last_activity_ms = lv_tick_get();
    return;
  }

  if (on) {
    update_restore_brightness();
    hal_display_set_brightness(s_restore_brightness);
    platform_log(PLATFORM_LOG_INFO, TAG, "Backlight ON (%d%%)",
                 (int)s_restore_brightness);
  } else {
    hal_display_set_brightness(0);
    platform_log(PLATFORM_LOG_INFO, TAG, "Backlight OFF (auto-timeout)");
  }
  s_backlight_on = on;
  s_last_activity_ms = lv_tick_get();
}

bool ui_backlight_is_on(void) {
  return s_backlight_on;
}

void ui_backlight_refresh_timeout(void) {
  s_last_activity_ms = lv_tick_get();
}

void ui_backlight_process(void) {
  if (!s_backlight_on || s_timeout_ms <= 0) return;

  if (lv_tick_elaps(s_last_activity_ms) > (uint32_t)s_timeout_ms) {
    ui_backlight_set_on(false);
  }
}

void ui_backlight_toggle(void) {
    ui_backlight_set_on(!s_backlight_on);
}

void ui_backlight_set_timeout(int32_t seconds) {
    s_timeout_ms = seconds * 1000;
    s_last_activity_ms = lv_tick_get();
    platform_log(PLATFORM_LOG_INFO, TAG, "Timeout updated: %ld ms",
                 (long)s_timeout_ms);
}
