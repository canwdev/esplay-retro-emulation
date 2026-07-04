#include "ui_settings.h"
#include "hal_audio.h"
#include "hal_display.h"
#include "hal_power.h"
#include "hal_settings.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "input_repeat.h"
#include "platform_log.h"
#include "platform_mem.h"
#include "ui_app.h"
#include "ui_backlight.h"
#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_home.h"
#include "ui_screen_test.h"
#include "ui_theme.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#ifdef TARGET_ESP32
#include "esp_app_desc.h"
#include "esp_system.h"
#endif

static const char *TAG = "ui_settings";

typedef enum {
  ROW_BACK = 0,
  ROW_BRIGHTNESS,
  ROW_VOLUME,
  ROW_THEME,
  ROW_BACKLIGHT_TIMEOUT,
  ROW_RESTART,
  ROW_SCREEN_TEST,
  ROW_COUNT
} settings_row_t;

static const int32_t s_timeout_options[] = {0, 5, 10, 30, 60};
static const int s_timeout_options_count = sizeof(s_timeout_options) / sizeof(s_timeout_options[0]);

static lv_obj_t *s_row_btns[ROW_COUNT];
static lv_obj_t *s_row_labels[ROW_COUNT];
static lv_obj_t *s_scroll;
static settings_row_t s_focus_row = ROW_BRIGHTNESS;
static lv_coord_t s_scroll_y = 0;
static int32_t s_brightness = 70;
static int32_t s_volume = 50;
static int32_t s_theme = 0;
static int32_t s_backlight_timeout = 30;
static ui_chrome_t s_chrome;
static input_repeat_state_t s_nav_repeat;

static const input_repeat_config_t s_settings_nav_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = 4,
    .max_scale = 4,
};

static const input_repeat_config_t s_settings_adjust_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = 4,
    .max_scale = 4,
};

static const input_repeat_config_t s_settings_discrete_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = 0,
    .max_scale = 1,
};

static void settings_save_backlight_timeout(void) {
  hal_settings_save(SettingBacklightTimeout, s_backlight_timeout);
}

static void settings_save_brightness(void) {
  hal_settings_save(SettingBacklight, s_brightness);
  hal_display_set_brightness((uint8_t)s_brightness);
}

static void settings_save_volume(void) {
  hal_settings_save(SettingAudioVolume, s_volume);
  hal_audio_set_volume((uint8_t)s_volume);
}

static void settings_save_theme(void) {
  hal_settings_save(SettingUiTheme, s_theme);
  ui_theme_set((int)s_theme);
}

static void settings_update_row_labels(void) {
  if (!s_row_labels[ROW_BRIGHTNESS])
    return;

  if (s_row_labels[ROW_BRIGHTNESS])
    lv_label_set_text_fmt(s_row_labels[ROW_BRIGHTNESS], "Brightness: %ld%%",
                          (long)s_brightness);
  if (s_row_labels[ROW_VOLUME])
    lv_label_set_text_fmt(s_row_labels[ROW_VOLUME], "Volume: %ld%%",
                          (long)s_volume);
  if (s_row_labels[ROW_THEME])
    lv_label_set_text_fmt(s_row_labels[ROW_THEME], "Theme: %s",
                          ui_theme_name((int)s_theme));
  if (s_row_labels[ROW_BACKLIGHT_TIMEOUT]) {
    if (s_backlight_timeout == 0)
      lv_label_set_text(s_row_labels[ROW_BACKLIGHT_TIMEOUT],
                        "Screen Off: Never");
    else
      lv_label_set_text_fmt(s_row_labels[ROW_BACKLIGHT_TIMEOUT],
                            "Screen Off: %lds",
                            (long)s_backlight_timeout);
  }
}

static lv_obj_t *settings_focused_obj(void) {
  return g_ui.input_group ? lv_group_get_focused(g_ui.input_group) : NULL;
}

static settings_row_t settings_row_from_obj(lv_obj_t *obj) {
  for (int i = 0; i < ROW_COUNT; i++) {
    if (s_row_btns[i] == obj)
      return (settings_row_t)i;
  }
  return ROW_COUNT;
}

static void settings_sync_focus_row_from_obj(lv_obj_t *obj) {
  settings_row_t row = settings_row_from_obj(obj);
  if (row != ROW_COUNT)
    s_focus_row = row;
}

static const input_repeat_config_t *settings_adjust_repeat_for_obj(lv_obj_t *obj) {
  settings_row_t row = settings_row_from_obj(obj);
  if (row == ROW_BRIGHTNESS || row == ROW_VOLUME)
    return &s_settings_adjust_repeat;
  if (row == ROW_THEME || row == ROW_BACKLIGHT_TIMEOUT)
    return &s_settings_discrete_repeat;
  return NULL;
}

static void settings_focus_move(int delta) {
  if (!g_ui.input_group || delta == 0)
    return;
  int steps = delta > 0 ? delta : -delta;
  while (steps-- > 0) {
    if (delta > 0)
      lv_group_focus_next(g_ui.input_group);
    else
      lv_group_focus_prev(g_ui.input_group);
  }
  settings_sync_focus_row_from_obj(settings_focused_obj());
}

static void settings_adjust_timeout(int delta) {
  if (delta == 0)
    return;
  int cur_idx = -1;
  for (int i = 0; i < s_timeout_options_count; i++) {
    if (s_timeout_options[i] == s_backlight_timeout) {
      cur_idx = i;
      break;
    }
  }
  if (cur_idx < 0)
    cur_idx = 0;
  if (delta > 0)
    cur_idx = (cur_idx + 1) % s_timeout_options_count;
  else
    cur_idx = (cur_idx + s_timeout_options_count - 1) % s_timeout_options_count;
  s_backlight_timeout = s_timeout_options[cur_idx];
  settings_save_backlight_timeout();
  ui_backlight_set_timeout(s_backlight_timeout);
  settings_update_row_labels();
}

static bool settings_adjust_obj(lv_obj_t *obj, int delta, int scale) {
  settings_row_t row = settings_row_from_obj(obj);
  if (row == ROW_COUNT || delta == 0)
    return false;
  if (scale < 1)
    scale = 1;

  if (row == ROW_BRIGHTNESS) {
    s_brightness += delta * scale;
    if (s_brightness < 1)
      s_brightness = 1;
    if (s_brightness > 100)
      s_brightness = 100;
    settings_save_brightness();
    settings_update_row_labels();
    return true;
  }
  if (row == ROW_VOLUME) {
    s_volume += delta * scale;
    if (s_volume < 0)
      s_volume = 0;
    if (s_volume > 100)
      s_volume = 100;
    settings_save_volume();
    settings_update_row_labels();
    return true;
  }
  if (row == ROW_THEME) {
    if (delta > 0)
      s_theme = (s_theme + 1) % UI_THEME_COUNT;
    else
      s_theme = (s_theme + UI_THEME_COUNT - 1) % UI_THEME_COUNT;
    settings_save_theme();
    settings_update_row_labels();
    ui_settings_create();
    return true;
  }
  if (row == ROW_BACKLIGHT_TIMEOUT) {
    settings_adjust_timeout(delta);
    return true;
  }
  return false;
}

static bool settings_activate_obj(lv_obj_t *obj) {
  settings_row_t row = settings_row_from_obj(obj);
  if (row == ROW_BACK) {
    ui_home_create();
    return true;
  }
  if (row == ROW_RESTART) {
    hal_system_reboot();
    return true;
  }
  if (row == ROW_SCREEN_TEST) {
    s_focus_row = ROW_SCREEN_TEST;
    if (s_scroll)
      s_scroll_y = lv_obj_get_scroll_y(s_scroll);
    ui_screen_test_open();
    return true;
  }
  return false;
}

#define SETTINGS_ROW_H 24
#define SETTINGS_ICON_W 20

static void settings_row_apply_focus(lv_obj_t *btn, bool focused) {
  if (!btn)
    return;

  lv_color_t bg = focused ? ui_theme_color_focus_bg() : ui_theme_color_panel();
  lv_color_t fg = focused ? ui_theme_color_accent() : ui_theme_color_text();
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_text_color(btn, fg, 0);

  for (uint32_t i = 0; i < lv_obj_get_child_cnt(btn); i++) {
    lv_obj_t *child = lv_obj_get_child(btn, i);
    if (lv_obj_check_type(child, &lv_label_class))
      lv_obj_set_style_text_color(child, fg, 0);
  }
}

static void settings_info_apply_focus(lv_obj_t *box, bool focused) {
  if (!box)
    return;

  lv_color_t bg = focused ? ui_theme_color_focus_bg() : ui_theme_color_panel();
  lv_color_t title = focused ? ui_theme_color_accent() : ui_theme_color_accent();
  lv_color_t body = focused ? ui_theme_color_accent() : ui_theme_color_text();

  lv_obj_set_style_bg_color(box, bg, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_outline_width(box, 0, 0);

  for (uint32_t i = 0; i < lv_obj_get_child_cnt(box); i++) {
    lv_obj_t *child = lv_obj_get_child(box, i);
    if (!lv_obj_check_type(child, &lv_label_class))
      continue;
    lv_obj_set_style_text_color(child, i == 0 ? title : body, 0);
  }
}

static lv_obj_t *settings_add_row(lv_obj_t *parent, settings_row_t row,
                                  const char *symbol, const char *text) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_height(btn, SETTINGS_ROW_H);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_STATE_TRICKLE);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_outline_width(btn, 0, 0);
  lv_obj_set_style_radius(btn, 0, 0);
  lv_obj_set_style_pad_top(btn, 0, 0);
  lv_obj_set_style_pad_bottom(btn, 0, 0);
  lv_obj_set_style_pad_left(btn, 6, 0);
  lv_obj_set_style_pad_right(btn, 6, 0);
  lv_obj_set_style_pad_column(btn, 4, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon = lv_label_create(btn);
  lv_obj_remove_style_all(icon);
  lv_label_set_text(icon, symbol ? symbol : "");
  lv_obj_set_style_text_font(icon, ui_font_builtin(), 0);
  lv_obj_set_width(icon, SETTINGS_ICON_W);
  ui_theme_style_label_row(icon, SETTINGS_ROW_H);
  lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_remove_style_all(lbl);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, ui_font_default(), 0);
  lv_obj_set_flex_grow(lbl, 1);
  lv_obj_set_width(lbl, 0);
  ui_theme_style_label_row(lbl, SETTINGS_ROW_H);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);

  settings_row_apply_focus(btn, false);
  s_row_labels[row] = lbl;
  return btn;
}

static void settings_row_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    settings_sync_focus_row_from_obj(obj);
    if (lv_obj_check_type(obj, &lv_button_class))
      settings_row_apply_focus(obj, true);
    else
      settings_info_apply_focus(obj, true);
    return;
  }
  if (code == LV_EVENT_DEFOCUSED) {
    if (lv_obj_check_type(obj, &lv_button_class))
      settings_row_apply_focus(obj, false);
    else
      settings_info_apply_focus(obj, false);
    return;
  }

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_indev_get_key(lv_indev_get_act());
    if (key == LV_KEY_DOWN) {
      settings_focus_move(1);
      return;
    }
    if (key == LV_KEY_UP) {
      settings_focus_move(-1);
      return;
    }
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
      settings_adjust_obj(obj, (key == LV_KEY_RIGHT) ? 1 : -1, 1);
    }
    return;
  }

  if (code != LV_EVENT_CLICKED)
    return;

  settings_activate_obj(obj);
}

static void settings_add_info_block(lv_obj_t *parent, const char *title,
                                    const char *body) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_width(box, LV_PCT(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(box, 0, 0);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_outline_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 8, 0);
  lv_obj_set_style_pad_row(box, 4, 0);
  lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  settings_info_apply_focus(box, false);

  if (g_ui.input_group) {
    lv_group_add_obj(g_ui.input_group, box);
  }
  lv_obj_add_event_cb(box, settings_row_event_handler, LV_EVENT_ALL, NULL);

  lv_obj_t *t = lv_label_create(box);
  lv_label_set_text(t, title);
  ui_theme_style_label_accent(t);

  lv_obj_t *b = lv_label_create(box);
  lv_label_set_text(b, body);
  lv_obj_set_width(b, LV_PCT(100));
  lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
  ui_theme_style_label_primary(b);
}

static int32_t settings_migrate_theme(int32_t theme) {
  if (theme >= 0 && theme < UI_THEME_COUNT)
    return theme;
  return UI_THEME_DARK;
}

void ui_settings_sync_volume(uint8_t volume) {
  if (volume > 100)
    volume = 100;
  s_volume = volume;
  hal_settings_save(SettingAudioVolume, s_volume);
  settings_update_row_labels();
}

void ui_settings_detach_ui(void) {
  memset(s_row_btns, 0, sizeof(s_row_btns));
  memset(s_row_labels, 0, sizeof(s_row_labels));
  s_scroll = NULL;
  input_repeat_reset(&s_nav_repeat);
}

void ui_settings_load_persisted(void) {
  if (hal_settings_load(SettingBacklight, &s_brightness) != 0)
    s_brightness = 70;
  if (hal_settings_load(SettingAudioVolume, &s_volume) != 0)
    s_volume = 50;
  if (hal_settings_load(SettingUiTheme, &s_theme) != 0)
    s_theme = 0;
  if (hal_settings_load(SettingBacklightTimeout, &s_backlight_timeout) != 0)
    s_backlight_timeout = 30;
  s_theme = settings_migrate_theme(s_theme);

  if (s_brightness < 10)
    s_brightness = 10;
  if (s_brightness > 100)
    s_brightness = 100;
  if (s_volume < 0)
    s_volume = 0;
  if (s_volume > 100)
    s_volume = 100;
  if (s_theme < 0 || s_theme >= UI_THEME_COUNT)
    s_theme = 0;

  hal_display_set_brightness((uint8_t)s_brightness);
  hal_audio_set_volume((uint8_t)s_volume);

  int32_t eq_preset = 0;
  hal_settings_load(SettingEqPreset, &eq_preset);
  hal_audio_set_eq_preset(eq_preset);

  ui_theme_set((int)s_theme);
}

void ui_settings_create(void) {
  if (!g_ui.input_group || !g_ui.screen) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "UI state not initialized");
    return;
  }

  settings_row_t restore_focus_row = s_focus_row;
  lv_coord_t restore_scroll_y = s_scroll_y;

  if (g_ui.current_page == PAGE_SETTINGS) {
    lv_obj_t *focused = lv_group_get_focused(g_ui.input_group);
    for (int i = 0; i < ROW_COUNT; i++) {
      if (focused == s_row_btns[i]) {
        restore_focus_row = (settings_row_t)i;
        break;
      }
    }
    if (s_scroll)
      restore_scroll_y = lv_obj_get_scroll_y(s_scroll);
  } else if (g_ui.current_page != PAGE_SCREEN_TEST) {
    restore_focus_row = ROW_BRIGHTNESS;
    restore_scroll_y = 0;
  }

  lv_group_remove_all_objs(g_ui.input_group);
  lv_group_set_wrap(g_ui.input_group, false);
  ui_settings_detach_ui();
  ui_chrome_detach(&s_chrome);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  s_chrome = ui_chrome_create(g_ui.screen, "Settings");

  lv_obj_t *scroll = lv_obj_create(g_ui.screen);
  s_scroll = scroll;
  lv_obj_remove_style_all(scroll);
  lv_obj_set_size(scroll, 310, 186);
  lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top() + 1);
  lv_obj_set_style_pad_bottom(scroll, 16, 0);
  lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll, 0, 0);
  lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(scroll, 2, 0);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
  ui_theme_style_scroll(scroll);

  s_row_btns[ROW_BACK] = settings_add_row(scroll, ROW_BACK, LV_SYMBOL_LEFT,
                                          "Back");
  lv_obj_add_event_cb(s_row_btns[ROW_BACK], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_BACK]);

  s_row_btns[ROW_BRIGHTNESS] =
      settings_add_row(scroll, ROW_BRIGHTNESS, LV_SYMBOL_IMAGE,
                       "Brightness");
  lv_obj_add_event_cb(s_row_btns[ROW_BRIGHTNESS], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_BRIGHTNESS]);

  s_row_btns[ROW_VOLUME] =
      settings_add_row(scroll, ROW_VOLUME, LV_SYMBOL_VOLUME_MAX, "Volume");
  lv_obj_add_event_cb(s_row_btns[ROW_VOLUME], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_VOLUME]);

  s_row_btns[ROW_THEME] =
      settings_add_row(scroll, ROW_THEME, LV_SYMBOL_TINT, "Theme");
  lv_obj_add_event_cb(s_row_btns[ROW_THEME], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_THEME]);

  s_row_btns[ROW_BACKLIGHT_TIMEOUT] =
      settings_add_row(scroll, ROW_BACKLIGHT_TIMEOUT, LV_SYMBOL_EYE_OPEN,
                       "Screen Off");
  lv_obj_add_event_cb(s_row_btns[ROW_BACKLIGHT_TIMEOUT],
                      settings_row_event_handler, LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_BACKLIGHT_TIMEOUT]);

  s_row_btns[ROW_RESTART] =
      settings_add_row(scroll, ROW_RESTART, LV_SYMBOL_REFRESH, "Reboot");
  lv_obj_add_event_cb(s_row_btns[ROW_RESTART], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_RESTART]);

  s_row_btns[ROW_SCREEN_TEST] =
      settings_add_row(scroll, ROW_SCREEN_TEST, LV_SYMBOL_IMAGE,
                       "Screen Test");
  lv_obj_add_event_cb(s_row_btns[ROW_SCREEN_TEST], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_SCREEN_TEST]);

  settings_update_row_labels();

  char storage_body[96];
  uint32_t tot = 0, free_space = 0;
  hal_storage_get_free_kb(&tot, &free_space);
  snprintf(storage_body, sizeof(storage_body), "Total %lu MB\nFree %lu MB",
           (unsigned long)tot / 1024, (unsigned long)free_space / 1024);
  settings_add_info_block(scroll, LV_SYMBOL_SD_CARD " Storage", storage_body);

  char memory_body[96];
  uint32_t flash = platform_flash_size();
  uint32_t psram = platform_psram_size();
  if (psram > 0) {
    snprintf(memory_body, sizeof(memory_body), "Flash %lu MB\nPSRAM %lu MB\nFree DRAM %lu KB",
             (unsigned long)flash / 1024 / 1024,
             (unsigned long)psram / 1024 / 1024,
             (unsigned long)platform_free_heap() / 1024);
  } else {
    snprintf(memory_body, sizeof(memory_body), "Flash %lu MB\nNo PSRAM\nFree DRAM %lu KB",
             (unsigned long)flash / 1024 / 1024,
             (unsigned long)platform_free_heap() / 1024);
  }
  settings_add_info_block(scroll, LV_SYMBOL_LIST " Memory", memory_body);

  hal_battery_t bat = {0};
  const char *charge_status = "Unknown";
  if (hal_power_read_battery(&bat)) {
    charge_status = bat.charging ? "Charging" : "Discharging";
  }
  char battery_body[96];
  snprintf(battery_body, sizeof(battery_body),
           "Status: %s\nVoltage: %d mV\nLevel: %d%%", charge_status,
           bat.millivolts, bat.percentage);
  settings_add_info_block(scroll, LV_SYMBOL_BATTERY_2 " Battery", battery_body);

  char about_body[160];
#ifdef TARGET_ESP32
  const esp_app_desc_t *desc = esp_app_get_description();
  if (desc) {
    snprintf(about_body, sizeof(about_body), "Launcher %s\nIDF %s\nBuilt %s %s",
             desc->version, desc->idf_ver, desc->date, desc->time);
  } else {
    snprintf(about_body, sizeof(about_body), "Launcher\nVersion info unavailable");
  }
#else
  snprintf(about_body, sizeof(about_body), "Launcher Sim\n%s",
           hal_system_app_version());
#endif
  settings_add_info_block(scroll, LV_SYMBOL_SETTINGS " About", about_body);

  lv_obj_t *hint = lv_label_create(g_ui.screen);
  lv_label_set_text(hint, LV_SYMBOL_UP LV_SYMBOL_DOWN " scroll  "
                           LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " adjust  A open  B back");
  ui_theme_style_label_secondary(hint);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

  lv_obj_scroll_to_y(scroll, restore_scroll_y, LV_ANIM_OFF);
  if (restore_focus_row >= 0 && restore_focus_row < ROW_COUNT &&
      s_row_btns[restore_focus_row]) {
    lv_group_focus_obj(s_row_btns[restore_focus_row]);
    s_focus_row = restore_focus_row;
  }
  s_scroll_y = restore_scroll_y;
  g_ui.current_page = PAGE_SETTINGS;
}

void ui_settings_handle_back(void) { ui_home_create(); }

void ui_settings_on_nav_key(uint32_t lv_key) {
  if (g_ui.current_page != PAGE_SETTINGS)
    return;

  lv_obj_t *obj = settings_focused_obj();
  if (lv_key == LV_KEY_UP) {
    settings_focus_move(-1);
    input_repeat_arm(&s_nav_repeat, LV_KEY_UP, lv_tick_get(),
                     &s_settings_nav_repeat);
    return;
  }
  if (lv_key == LV_KEY_DOWN) {
    settings_focus_move(1);
    input_repeat_arm(&s_nav_repeat, LV_KEY_DOWN, lv_tick_get(),
                     &s_settings_nav_repeat);
    return;
  }
  if (lv_key == LV_KEY_LEFT || lv_key == LV_KEY_RIGHT) {
    const input_repeat_config_t *config = settings_adjust_repeat_for_obj(obj);
    if (!config)
      return;
    settings_adjust_obj(obj, (lv_key == LV_KEY_RIGHT) ? 1 : -1, 1);
    input_repeat_arm(&s_nav_repeat, lv_key, lv_tick_get(), config);
    return;
  }
  if (lv_key == LV_KEY_ENTER)
    settings_activate_obj(obj);
}

void ui_settings_on_nav_hold_tick(bool up, bool down, bool left, bool right) {
  if (g_ui.current_page != PAGE_SETTINGS) {
    input_repeat_reset(&s_nav_repeat);
    return;
  }

  int held_count = (up ? 1 : 0) + (down ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
  if (held_count != 1) {
    input_repeat_reset(&s_nav_repeat);
    return;
  }

  lv_obj_t *obj = settings_focused_obj();
  uint32_t dir = 0;
  const input_repeat_config_t *config = NULL;
  if (up) {
    dir = LV_KEY_UP;
    config = &s_settings_nav_repeat;
  } else if (down) {
    dir = LV_KEY_DOWN;
    config = &s_settings_nav_repeat;
  } else if (left) {
    dir = LV_KEY_LEFT;
    config = settings_adjust_repeat_for_obj(obj);
  } else if (right) {
    dir = LV_KEY_RIGHT;
    config = settings_adjust_repeat_for_obj(obj);
  }

  if (!config) {
    input_repeat_reset(&s_nav_repeat);
    return;
  }

  uint16_t repeat_count = 0;
  if (!input_repeat_tick(&s_nav_repeat, true, dir, lv_tick_get(), config,
                         &repeat_count))
    return;

  int scale = input_repeat_scale_for_count(config, repeat_count);
  if (dir == LV_KEY_UP)
    settings_focus_move(-scale);
  else if (dir == LV_KEY_DOWN)
    settings_focus_move(scale);
  else
    settings_adjust_obj(settings_focused_obj(),
                        (dir == LV_KEY_RIGHT) ? 1 : -1, scale);
}

bool ui_settings_uses_direct_nav(void) {
  return g_ui.current_page == PAGE_SETTINGS;
}
