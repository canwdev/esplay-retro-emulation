#include "ui_home.h"
#include "file_manager.h"
#include "ui_app.h"
#include "ui_settings.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "lvgl.h"
#include "power.h"
#include <string.h>

static const char *TAG = "ui_home";

typedef enum {
  HOME_TILE_FILES = 0,
  HOME_TILE_SETTINGS = 1,
} home_tile_t;

static home_tile_t s_last_home_tile = HOME_TILE_FILES;

static void home_update_selected_label(lv_obj_t *obj) {
  if (!g_ui.menu_selected_label)
    return;
  if (obj == g_ui.home_btn_files)
    lv_label_set_text(g_ui.menu_selected_label, "Files");
  else if (obj == g_ui.home_btn_settings)
    lv_label_set_text(g_ui.menu_selected_label, "Settings");
}

static void btn_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_indev_get_key(lv_indev_get_act());
    if (key == LV_KEY_RIGHT)
      lv_group_focus_next(lv_group_get_default());
    else if (key == LV_KEY_LEFT)
      lv_group_focus_prev(lv_group_get_default());
    return;
  }

  if (code == LV_EVENT_FOCUSED) {
    if (obj == g_ui.home_btn_files)
      s_last_home_tile = HOME_TILE_FILES;
    else if (obj == g_ui.home_btn_settings)
      s_last_home_tile = HOME_TILE_SETTINGS;
    else
      ESP_LOGW(TAG, "Unknown button focused");
    home_update_selected_label(obj);
  } else if (code == LV_EVENT_CLICKED) {
    if (obj == g_ui.home_btn_files) {
      fm_reset_cwd();
      fm_create();
    } else if (obj == g_ui.home_btn_settings) {
      ui_settings_create();
    }
  }
}

static lv_obj_t *create_home_tile(lv_obj_t *parent, const char *symbol,
                                  const char *caption) {
  lv_obj_t *btn = lv_btn_create(parent);
  if (!btn)
    return NULL;

  ui_theme_style_list_btn(btn);
  lv_obj_set_size(btn, 96, 72);
  lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_ALL, NULL);

  lv_obj_t *col = lv_obj_create(btn);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);

  lv_obj_t *sym = lv_label_create(col);
  lv_label_set_text(sym, symbol);
  ui_theme_style_label_accent(sym);

  lv_obj_t *lbl = lv_label_create(col);
  lv_label_set_text(lbl, caption);
  ui_theme_style_label_primary(lbl);

  return btn;
}

void ui_home_create(void) {
  if (!g_ui.input_group || !g_ui.screen) {
    ESP_LOGE(TAG, "UI state not initialized");
    return;
  }

  lv_group_remove_all_objs(g_ui.input_group);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  char buffer[64];
  ui_app_get_time(buffer, sizeof(buffer));

  g_ui.time_label = lv_label_create(g_ui.screen);
  lv_label_set_text(g_ui.time_label, buffer);
  ui_theme_style_label_secondary(g_ui.time_label);
  lv_obj_align(g_ui.time_label, LV_ALIGN_TOP_LEFT, 6, 4);

  lv_obj_t *title = lv_label_create(g_ui.screen);
  lv_label_set_text(title, "ESPLAY");
  ui_theme_style_label_accent(title);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  g_ui.battery_label = lv_label_create(g_ui.screen);
  lv_label_set_text(g_ui.battery_label, LV_SYMBOL_BATTERY_FULL);
  ui_theme_style_label_secondary(g_ui.battery_label);
  lv_obj_align(g_ui.battery_label, LV_ALIGN_TOP_RIGHT, -6, 4);

  lv_obj_t *center = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(center);
  lv_obj_set_size(center, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(center, 0, 0);
  lv_obj_set_style_pad_row(center, 12, 0);

  g_ui.menu_selected_label = lv_label_create(center);
  lv_label_set_text(g_ui.menu_selected_label, "Files");
  ui_theme_style_label_accent(g_ui.menu_selected_label);

  lv_obj_t *row = lv_obj_create(center);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 24, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);

  g_ui.home_btn_files =
      create_home_tile(row, LV_SYMBOL_SD_CARD, "Files");
  g_ui.home_btn_settings =
      create_home_tile(row, LV_SYMBOL_SETTINGS, "Settings");

  if (g_ui.home_btn_files)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_files);
  if (g_ui.home_btn_settings)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_settings);

  lv_obj_t *focus_btn = (s_last_home_tile == HOME_TILE_SETTINGS)
                            ? g_ui.home_btn_settings
                            : g_ui.home_btn_files;
  if (focus_btn) {
    lv_group_focus_obj(focus_btn);
    home_update_selected_label(focus_btn);
  }

  g_ui.current_page = PAGE_HOME;
}

void ui_home_update_status(void) {
  if (g_ui.current_page != PAGE_HOME || !g_ui.time_label || !g_ui.battery_label)
    return;

  char buffer[64];
  ui_app_get_time(buffer, sizeof(buffer));
  lv_label_set_text(g_ui.time_label, buffer);

  battery_state bat;
  battery_level_read(&bat);

  if (bat.state == FULL_CHARGED || bat.state == CHARGING)
    lv_label_set_text(g_ui.battery_label, LV_SYMBOL_CHARGE);
  else if (bat.percentage > 75)
    lv_label_set_text(g_ui.battery_label, LV_SYMBOL_BATTERY_FULL);
  else if (bat.percentage > 50)
    lv_label_set_text(g_ui.battery_label, LV_SYMBOL_BATTERY_3);
  else if (bat.percentage > 25)
    lv_label_set_text(g_ui.battery_label, LV_SYMBOL_BATTERY_2);
  else if (bat.percentage > 5)
    lv_label_set_text(g_ui.battery_label, LV_SYMBOL_BATTERY_1);
  else
    lv_label_set_text(g_ui.battery_label, LV_SYMBOL_BATTERY_EMPTY);
}
