#include "ui_settings.h"
#include "audio.h"
#include "lcd.h"
#include "settings.h"
#include "ui_app.h"
#include "ui_home.h"
#include "ui_screen_test.h"
#include "ui_theme.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "lvgl.h"
#include "power.h"
#include "sdcard.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_settings";

typedef enum {
  ROW_BACK = 0,
  ROW_BRIGHTNESS,
  ROW_VOLUME,
  ROW_THEME,
  ROW_SCREEN_TEST,
  ROW_COUNT
} settings_row_t;

static lv_obj_t *s_row_btns[ROW_COUNT];
static lv_obj_t *s_scroll;
static settings_row_t s_focus_row = ROW_BRIGHTNESS;
static lv_coord_t s_scroll_y = 0;
static int32_t s_brightness = 70;
static int32_t s_volume = 50;
static int32_t s_theme = 0;

static void settings_save_brightness(void) {
  settings_save(SettingBacklight, s_brightness);
  lcd_set_brightness((uint8_t)s_brightness);
}

static void settings_save_volume(void) {
  settings_save(SettingAudioVolume, s_volume);
  audio_set_volume((uint8_t)s_volume);
}

static void settings_save_theme(void) {
  settings_save(SettingUiTheme, s_theme);
  ui_theme_set((int)s_theme);
}

static void settings_update_row_labels(void) {
  if (s_row_btns[ROW_BRIGHTNESS]) {
    lv_obj_t *lbl = lv_obj_get_child(s_row_btns[ROW_BRIGHTNESS], 0);
    if (lbl)
      lv_label_set_text_fmt(lbl, LV_SYMBOL_IMAGE " Brightness: %ld%%",
                            (long)s_brightness);
  }
  if (s_row_btns[ROW_VOLUME]) {
    lv_obj_t *lbl = lv_obj_get_child(s_row_btns[ROW_VOLUME], 0);
    if (lbl)
      lv_label_set_text_fmt(lbl, LV_SYMBOL_VOLUME_MAX " Volume: %ld%%",
                            (long)s_volume);
  }
  if (s_row_btns[ROW_THEME]) {
    lv_obj_t *lbl = lv_obj_get_child(s_row_btns[ROW_THEME], 0);
    if (lbl)
      lv_label_set_text_fmt(lbl, LV_SYMBOL_TINT " Theme: %s",
                            ui_theme_name((int)s_theme));
  }
}

static lv_obj_t *settings_add_row(lv_obj_t *parent, const char *text) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_height(btn, 28);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  ui_theme_style_label_primary(lbl);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  ui_theme_style_list_btn(btn);
  return btn;
}

static void settings_row_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_indev_get_key(lv_indev_get_act());
    if (key == LV_KEY_DOWN) {
      if (obj == s_row_btns[ROW_SCREEN_TEST] && s_scroll &&
          lv_obj_get_scroll_bottom(s_scroll) > 0) {
        lv_obj_scroll_by_bounded(s_scroll, 0, -36, LV_ANIM_ON);
        return;
      }
      lv_group_focus_next(lv_group_get_default());
    } else if (key == LV_KEY_UP) {
      if (obj == s_row_btns[ROW_BACK] && s_scroll &&
          lv_obj_get_scroll_top(s_scroll) > 0) {
        lv_obj_scroll_by_bounded(s_scroll, 0, 36, LV_ANIM_ON);
        return;
      }
      lv_group_focus_prev(lv_group_get_default());
    } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
      int delta = (key == LV_KEY_RIGHT) ? 5 : -5;
      if (obj == s_row_btns[ROW_BRIGHTNESS]) {
        s_brightness += delta;
        if (s_brightness < 10)
          s_brightness = 10;
        if (s_brightness > 100)
          s_brightness = 100;
        settings_save_brightness();
        settings_update_row_labels();
      } else if (obj == s_row_btns[ROW_VOLUME]) {
        s_volume += delta;
        if (s_volume < 0)
          s_volume = 0;
        if (s_volume > 100)
          s_volume = 100;
        settings_save_volume();
        settings_update_row_labels();
      } else if (obj == s_row_btns[ROW_THEME]) {
        if (key == LV_KEY_RIGHT)
          s_theme = (s_theme + 1) % UI_THEME_COUNT;
        else
          s_theme = (s_theme + UI_THEME_COUNT - 1) % UI_THEME_COUNT;
        settings_save_theme();
        settings_update_row_labels();
        ui_settings_create();
      }
    }
    return;
  }

  if (code != LV_EVENT_CLICKED)
    return;

  if (obj == s_row_btns[ROW_BACK])
    ui_home_create();
  else if (obj == s_row_btns[ROW_SCREEN_TEST]) {
    s_focus_row = ROW_SCREEN_TEST;
    if (s_scroll)
      s_scroll_y = lv_obj_get_scroll_y(s_scroll);
    ui_screen_test_open();
  }
}

static void settings_add_info_block(lv_obj_t *parent, const char *title,
                                    const char *body) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  ui_theme_style_panel(box);
  lv_obj_set_width(box, LV_PCT(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(box, 8, 0);
  lv_obj_set_style_pad_row(box, 4, 0);

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
  if (theme >= 0 && theme <= 1)
    return theme;
  if (theme == 2)
    return UI_THEME_RED_DARK;
  if (theme == 3)
    return UI_THEME_YELLOW_DARK;
  if (theme == 4)
    return UI_THEME_PURPLE_DARK;
  if (theme == 5)
    return UI_THEME_BLUE_DARK;
  if (theme >= 0 && theme < UI_THEME_COUNT)
    return theme;
  return UI_THEME_DARK;
}

void ui_settings_load_persisted(void) {
  if (settings_load(SettingBacklight, &s_brightness) != 0)
    s_brightness = 70;
  if (settings_load(SettingAudioVolume, &s_volume) != 0)
    s_volume = 50;
  if (settings_load(SettingUiTheme, &s_theme) != 0)
    s_theme = 0;
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

  lcd_set_brightness((uint8_t)s_brightness);
  audio_set_volume((uint8_t)s_volume);
  ui_theme_set((int)s_theme);
}

void ui_settings_create(void) {
  if (!g_ui.input_group || !g_ui.screen) {
    ESP_LOGE(TAG, "UI state not initialized");
    return;
  }

  if (g_ui.current_page == PAGE_SETTINGS) {
    lv_obj_t *focused = lv_group_get_focused(g_ui.input_group);
    for (int i = 0; i < ROW_COUNT; i++) {
      if (focused == s_row_btns[i]) {
        s_focus_row = (settings_row_t)i;
        break;
      }
    }
    if (s_scroll)
      s_scroll_y = lv_obj_get_scroll_y(s_scroll);
  } else if (g_ui.current_page != PAGE_SCREEN_TEST) {
    s_focus_row = ROW_BRIGHTNESS;
    s_scroll_y = 0;
  }

  lv_group_remove_all_objs(g_ui.input_group);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  lv_obj_t *title = lv_label_create(g_ui.screen);
  lv_label_set_text(title, "Settings");
  ui_theme_style_label_accent(title);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *scroll = lv_obj_create(g_ui.screen);
  s_scroll = scroll;
  lv_obj_remove_style_all(scroll);
  lv_obj_set_size(scroll, 300, 196);
  lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll, 0, 0);
  lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(scroll, 6, 0);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
  ui_theme_style_scroll(scroll);

  s_row_btns[ROW_BACK] = settings_add_row(scroll, LV_SYMBOL_LEFT " Back");
  lv_obj_add_event_cb(s_row_btns[ROW_BACK], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_BACK]);

  s_row_btns[ROW_BRIGHTNESS] =
      settings_add_row(scroll, LV_SYMBOL_IMAGE " Brightness");
  lv_obj_add_event_cb(s_row_btns[ROW_BRIGHTNESS], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_BRIGHTNESS]);

  s_row_btns[ROW_VOLUME] =
      settings_add_row(scroll, LV_SYMBOL_VOLUME_MAX " Volume");
  lv_obj_add_event_cb(s_row_btns[ROW_VOLUME], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_VOLUME]);

  s_row_btns[ROW_THEME] = settings_add_row(scroll, LV_SYMBOL_TINT " Theme");
  lv_obj_add_event_cb(s_row_btns[ROW_THEME], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_THEME]);

  s_row_btns[ROW_SCREEN_TEST] =
      settings_add_row(scroll, LV_SYMBOL_IMAGE " Screen Test");
  lv_obj_add_event_cb(s_row_btns[ROW_SCREEN_TEST], settings_row_event_handler,
                      LV_EVENT_ALL, NULL);
  lv_group_add_obj(g_ui.input_group, s_row_btns[ROW_SCREEN_TEST]);

  settings_update_row_labels();

  char storage_body[96];
  uint32_t tot = 0, free_space = 0;
  sdcard_get_free_space(&tot, &free_space);
  snprintf(storage_body, sizeof(storage_body), "Total %lu MB\nFree %lu MB",
           (unsigned long)tot / 1024, (unsigned long)free_space / 1024);
  settings_add_info_block(scroll, LV_SYMBOL_SD_CARD " Storage", storage_body);

  battery_state bat;
  battery_level_read(&bat);
  const char *charge_status =
      (bat.state == CHARGING)       ? "Charging"
      : (bat.state == FULL_CHARGED) ? "Fully Charged"
                                    : "Discharging";
  char battery_body[96];
  snprintf(battery_body, sizeof(battery_body),
           "Status: %s\nVoltage: %d mV\nLevel: %d%%", charge_status,
           bat.millivolts, bat.percentage);
  settings_add_info_block(scroll, LV_SYMBOL_BATTERY_2 " Battery", battery_body);

  char about_body[160];
  const esp_app_desc_t *desc = esp_app_get_description();
  if (desc) {
    snprintf(about_body, sizeof(about_body), "Launcher %s\nIDF %s\nBuilt %s %s",
             desc->version, desc->idf_ver, desc->date, desc->time);
  } else {
    strlcpy(about_body, "Launcher\nVersion info unavailable", sizeof(about_body));
  }
  settings_add_info_block(scroll, LV_SYMBOL_SETTINGS " About", about_body);

  lv_obj_t *hint = lv_label_create(g_ui.screen);
  lv_label_set_text(hint, LV_SYMBOL_UP LV_SYMBOL_DOWN " scroll  "
                           LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " adjust  A open  B back");
  ui_theme_style_label_secondary(hint);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

  lv_obj_scroll_to_y(scroll, s_scroll_y, LV_ANIM_OFF);
  if (s_row_btns[s_focus_row])
    lv_group_focus_obj(s_row_btns[s_focus_row]);
  g_ui.current_page = PAGE_SETTINGS;
}

void ui_settings_handle_back(void) { ui_home_create(); }
