#include "ui_home.h"
#include "file_manager.h"
#include "preview_audio.h"
#include "hal_display.h"
#include "platform_log.h"
#include "preview.h"
#include "ui_app.h"
#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_settings.h"
#include "ui_theme.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "ui_home";

typedef enum {
  HOME_TILE_FILES = 0,
  HOME_TILE_MUSIC = 1,
  HOME_TILE_SETTINGS = 2,
} home_tile_t;

static ui_chrome_t s_chrome;
static home_tile_t s_last_home_tile = HOME_TILE_FILES;

static const char *home_tile_title(home_tile_t tile) {
  if (tile == HOME_TILE_MUSIC)
    return "Music Player";
  if (tile == HOME_TILE_SETTINGS)
    return "Settings";
  return "File Manager";
}

static void home_update_selected_label(lv_obj_t *obj) {
  if (!g_ui.menu_selected_label)
    return;
  if (obj == g_ui.home_btn_files)
    lv_label_set_text(g_ui.menu_selected_label, home_tile_title(HOME_TILE_FILES));
  else if (obj == g_ui.home_btn_music)
    lv_label_set_text(g_ui.menu_selected_label, home_tile_title(HOME_TILE_MUSIC));
  else if (obj == g_ui.home_btn_settings)
    lv_label_set_text(g_ui.menu_selected_label, home_tile_title(HOME_TILE_SETTINGS));
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
    else if (obj == g_ui.home_btn_music)
      s_last_home_tile = HOME_TILE_MUSIC;
    else if (obj == g_ui.home_btn_settings)
      s_last_home_tile = HOME_TILE_SETTINGS;
    else
      platform_log(PLATFORM_LOG_WARN, TAG, "Unknown button focused");
    home_update_selected_label(obj);
  } else if (code == LV_EVENT_CLICKED) {
    if (obj == g_ui.home_btn_files) {
      platform_log(PLATFORM_LOG_INFO, TAG, "enter Files");
      fm_reset_cwd();
      fm_create();
    } else if (obj == g_ui.home_btn_music) {
      if (preview_audio_restore_foreground(g_ui.screen, g_ui.input_group)) {
        platform_log(PLATFORM_LOG_INFO, TAG, "restore Music");
        g_ui.current_page = PAGE_FILES;
      }
    } else if (obj == g_ui.home_btn_settings) {
      platform_log(PLATFORM_LOG_INFO, TAG, "enter Settings");
      ui_settings_create();
    }
  }
}

static lv_obj_t *create_home_tile(lv_obj_t *parent, const char *symbol) {
  lv_obj_t *btn = lv_btn_create(parent);
  if (!btn)
    return NULL;

  ui_theme_style_list_btn(btn);
  lv_obj_set_size(btn, 112, 96);
  lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_ALL, NULL);

  lv_obj_t *sym = lv_label_create(btn);
  lv_label_set_text(sym, symbol);
  lv_obj_set_style_text_font(sym, ui_font_icon(), 0);
  ui_theme_style_label_accent(sym);
  lv_obj_center(sym);

  return btn;
}

void ui_home_create(void) {
  if (!g_ui.input_group || !g_ui.screen) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "UI state not initialized");
    return;
  }

  lv_group_remove_all_objs(g_ui.input_group);
  ui_settings_detach_ui();
  ui_chrome_detach(&s_chrome);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  s_chrome = ui_chrome_create(g_ui.screen, "ESPLAY NEO FIRMWARE");

  lv_coord_t body_top = ui_chrome_body_top();
  lv_obj_t *center = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(center);
  lv_obj_set_width(center, LV_PCT(100));
  lv_obj_set_height(center, HAL_DISPLAY_HEIGHT - body_top);
  lv_obj_align(center, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(center, 0, 0);
  lv_obj_set_style_pad_row(center, 12, 0);

  g_ui.menu_selected_label = lv_label_create(center);
  lv_label_set_text(g_ui.menu_selected_label,
                    home_tile_title(s_last_home_tile));
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

  g_ui.home_btn_files = create_home_tile(row, LV_SYMBOL_SD_CARD);
  g_ui.home_btn_music = NULL;
  if (preview_audio_session_is_active())
    g_ui.home_btn_music = create_home_tile(row, LV_SYMBOL_AUDIO);
  g_ui.home_btn_settings = create_home_tile(row, LV_SYMBOL_SETTINGS);

  if (g_ui.home_btn_files)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_files);
  if (g_ui.home_btn_music)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_music);
  if (g_ui.home_btn_settings)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_settings);

  lv_obj_t *focus_btn = g_ui.home_btn_files;
  if (s_last_home_tile == HOME_TILE_MUSIC && g_ui.home_btn_music)
    focus_btn = g_ui.home_btn_music;
  else if (s_last_home_tile == HOME_TILE_SETTINGS)
    focus_btn = g_ui.home_btn_settings;
  if (focus_btn) {
    lv_group_focus_obj(focus_btn);
    home_update_selected_label(focus_btn);
  }

  g_ui.current_page = PAGE_HOME;
}
