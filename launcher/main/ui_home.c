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

#define HOME_TILE_W        88
#define HOME_TILE_H        76
#define HOME_TILE_GAP      12
#define HOME_VIEWPORT_H    84

typedef enum {
  HOME_TILE_FILES = 0,
  HOME_TILE_MUSIC = 1,
  HOME_TILE_SETTINGS = 2,
} home_tile_t;

static ui_chrome_t s_chrome;
static home_tile_t s_last_home_tile = HOME_TILE_FILES;
static lv_obj_t *s_tiles_viewport;
static lv_obj_t *s_tiles_row;
static bool s_home_rebuilding;
static void home_row_set_x(void *obj, int32_t value);

static const char *home_tile_title(home_tile_t tile) {
  if (tile == HOME_TILE_MUSIC)
    return "Music Player";
  if (tile == HOME_TILE_SETTINGS)
    return "Settings";
  return "File Manager";
}

static lv_obj_t *home_button_for_tile(home_tile_t tile) {
  if (tile == HOME_TILE_MUSIC)
    return g_ui.home_btn_music;
  if (tile == HOME_TILE_SETTINGS)
    return g_ui.home_btn_settings;
  return g_ui.home_btn_files;
}

static void home_detach_ui(void) {
  if (s_tiles_row)
    lv_anim_del(s_tiles_row, home_row_set_x);

  s_tiles_viewport = NULL;
  s_tiles_row = NULL;
  g_ui.home_btn_files = NULL;
  g_ui.home_btn_music = NULL;
  g_ui.home_btn_settings = NULL;
  g_ui.menu_selected_label = NULL;
}

static void home_prepare_leave(home_tile_t tile) {
  s_last_home_tile = tile;
  s_home_rebuilding = true;
  home_detach_ui();
}

static void home_row_set_x(void *obj, int32_t value) {
  lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)value);
}

static void home_position_tiles(lv_obj_t *focused, bool animate) {
  if (!s_tiles_viewport || !s_tiles_row || !lv_obj_is_valid(s_tiles_viewport) ||
      !lv_obj_is_valid(s_tiles_row))
    return;

  lv_obj_update_layout(s_tiles_viewport);
  lv_obj_update_layout(s_tiles_row);

  lv_coord_t viewport_w = lv_obj_get_content_width(s_tiles_viewport);
  if (viewport_w <= 0)
    viewport_w = lv_obj_get_width(s_tiles_viewport);
  lv_coord_t row_w = lv_obj_get_width(s_tiles_row);
  if (viewport_w <= 0 || row_w <= 0)
    return;

  lv_coord_t target_x = 0;
  if (row_w <= viewport_w) {
    target_x = (viewport_w - row_w) / 2;
  } else if (focused && lv_obj_is_valid(focused)) {
    lv_coord_t obj_x = lv_obj_get_x(focused);
    lv_coord_t obj_w = lv_obj_get_width(focused);
    target_x = viewport_w / 2 - (obj_x + obj_w / 2);
    lv_coord_t min_x = viewport_w - row_w;
    if (target_x > 0)
      target_x = 0;
    if (target_x < min_x)
      target_x = min_x;
  }

  if (!animate || lv_obj_get_x(s_tiles_row) == target_x) {
    lv_obj_set_x(s_tiles_row, target_x);
    return;
  }

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_tiles_row);
  lv_anim_set_values(&a, lv_obj_get_x(s_tiles_row), target_x);
  lv_anim_set_time(&a, 140);
  lv_anim_set_exec_cb(&a, home_row_set_x);
  lv_anim_start(&a);
}

static void home_update_selected_label(lv_obj_t *obj) {
  if (s_home_rebuilding || !g_ui.menu_selected_label ||
      !lv_obj_is_valid(g_ui.menu_selected_label))
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

  if (s_home_rebuilding)
    return;

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
    home_position_tiles(obj, true);
  } else if (code == LV_EVENT_CLICKED) {
    if (obj == g_ui.home_btn_files) {
      home_prepare_leave(HOME_TILE_FILES);
      platform_log(PLATFORM_LOG_INFO, TAG, "enter Files");
      fm_reset_cwd();
      fm_create();
    } else if (obj == g_ui.home_btn_music) {
      home_prepare_leave(HOME_TILE_MUSIC);
      if (preview_audio_restore_foreground(g_ui.screen, g_ui.input_group)) {
        platform_log(PLATFORM_LOG_INFO, TAG, "restore Music");
        g_ui.current_page = PAGE_FILES;
      }
    } else if (obj == g_ui.home_btn_settings) {
      home_prepare_leave(HOME_TILE_SETTINGS);
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
  lv_obj_set_size(btn, HOME_TILE_W, HOME_TILE_H);
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

  s_home_rebuilding = true;
  home_detach_ui();
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

  s_tiles_viewport = lv_obj_create(center);
  lv_obj_remove_style_all(s_tiles_viewport);
  lv_obj_set_width(s_tiles_viewport, LV_PCT(100));
  lv_obj_set_height(s_tiles_viewport, HOME_VIEWPORT_H);
  lv_obj_set_style_bg_opa(s_tiles_viewport, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_tiles_viewport, 0, 0);
  lv_obj_set_style_pad_all(s_tiles_viewport, 0, 0);
  lv_obj_remove_flag(s_tiles_viewport, LV_OBJ_FLAG_SCROLLABLE);

  s_tiles_row = lv_obj_create(s_tiles_viewport);
  lv_obj_remove_style_all(s_tiles_row);
  lv_obj_set_size(s_tiles_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_tiles_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_tiles_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_tiles_row, HOME_TILE_GAP, 0);
  lv_obj_set_style_bg_opa(s_tiles_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_tiles_row, 0, 0);
  lv_obj_align(s_tiles_row, LV_ALIGN_LEFT_MID, 0, 0);

  g_ui.home_btn_files = create_home_tile(s_tiles_row, LV_SYMBOL_SD_CARD);
  g_ui.home_btn_music = NULL;
  if (preview_audio_session_is_active())
    g_ui.home_btn_music = create_home_tile(s_tiles_row, LV_SYMBOL_AUDIO);
  g_ui.home_btn_settings = create_home_tile(s_tiles_row, LV_SYMBOL_SETTINGS);

  if (g_ui.home_btn_files)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_files);
  if (g_ui.home_btn_music)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_music);
  if (g_ui.home_btn_settings)
    lv_group_add_obj(g_ui.input_group, g_ui.home_btn_settings);

  lv_obj_t *focus_btn = home_button_for_tile(s_last_home_tile);
  if (!focus_btn)
    focus_btn = g_ui.home_btn_files ? g_ui.home_btn_files : g_ui.home_btn_settings;
  if (focus_btn) {
    lv_group_focus_obj(focus_btn);
    home_update_selected_label(focus_btn);
    home_position_tiles(focus_btn, false);
  }

  g_ui.current_page = PAGE_HOME;
  s_home_rebuilding = false;
}
