#include "file_manager.h"
#include "preview.h"
#include "ui_app.h"
#include "ui_backlight.h"
#include "ui_chrome.h"
#include "ui_home.h"
#include "ui_screen_test.h"
#include "ui_settings.h"
#include "ui_theme.h"

#include "lvgl.h"

#include <string.h>

static ui_chrome_t s_settings_chrome;

void ui_backlight_init(void) {
}

bool ui_backlight_is_on(void) {
  return true;
}

void ui_backlight_set_on(bool on) {
  (void)on;
}

void ui_backlight_refresh_timeout(void) {
}

void ui_backlight_process(void) {
}

bool preview_is_active(void) {
  return false;
}

bool preview_on_key(const input_gamepad_state *gp, const bool edge[]) {
  (void)gp;
  (void)edge;
  return false;
}

void preview_on_timer(void) {
}

void preview_close(void) {
}

bool fm_uses_direct_nav(void) {
  return false;
}

void fm_on_nav_key(uint32_t lv_key) {
  (void)lv_key;
}

void fm_on_nav_hold_tick(bool up, bool down, bool left, bool right) {
  (void)up;
  (void)down;
  (void)left;
  (void)right;
}

void fm_handle_back(void) {
  if (g_ui.current_page == PAGE_FILES)
    ui_home_create();
}

void fm_handle_menu_on_focus(void) {
}

bool fm_close_top_dialog(void) {
  return false;
}

bool fm_has_open_dialog(void) {
  return false;
}

void fm_reset_cwd(void) {
}

static void files_back_to_home_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    ui_home_create();
}

void fm_create(void) {
  lv_group_remove_all_objs(g_ui.input_group);
  ui_chrome_detach(&s_settings_chrome);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  s_settings_chrome = ui_chrome_create(g_ui.screen, "Files");

  lv_obj_t *btn = lv_button_create(g_ui.screen);
  ui_theme_style_btn(btn);
  lv_obj_set_size(btn, 220, 40);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(btn, files_back_to_home_cb, LV_EVENT_ALL, NULL);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, "Not implemented");
  lv_obj_center(label);

  lv_group_add_obj(g_ui.input_group, btn);
  lv_group_focus_obj(btn);

  g_ui.current_page = PAGE_FILES;
}

void ui_settings_detach_ui(void) {
  ui_chrome_detach(&s_settings_chrome);
}

static void settings_btn_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if (code != LV_EVENT_CLICKED)
    return;

  const char *txt = NULL;
  lv_obj_t *child = lv_obj_get_child(obj, 0);
  if (child && lv_obj_check_type(child, &lv_label_class))
    txt = lv_label_get_text(child);

  if (txt && strcmp(txt, "Screen Test") == 0) {
    ui_screen_test_open();
  } else {
    ui_home_create();
  }
}

void ui_settings_create(void) {
  lv_group_remove_all_objs(g_ui.input_group);
  ui_chrome_detach(&s_settings_chrome);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  s_settings_chrome = ui_chrome_create(g_ui.screen, "Settings");

  lv_obj_t *col = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 10, 0);
  lv_obj_set_style_pad_row(col, 8, 0);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top() + 6);

  lv_obj_t *btn_test = lv_button_create(col);
  ui_theme_style_btn(btn_test);
  lv_obj_set_size(btn_test, LV_PCT(100), 40);
  lv_obj_add_event_cb(btn_test, settings_btn_event, LV_EVENT_ALL, NULL);
  lv_obj_t *lbl_test = lv_label_create(btn_test);
  lv_label_set_text(lbl_test, "Screen Test");
  lv_obj_center(lbl_test);

  lv_obj_t *btn_back = lv_button_create(col);
  ui_theme_style_btn(btn_back);
  lv_obj_set_size(btn_back, LV_PCT(100), 40);
  lv_obj_add_event_cb(btn_back, settings_btn_event, LV_EVENT_ALL, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Back");
  lv_obj_center(lbl_back);

  lv_group_add_obj(g_ui.input_group, btn_test);
  lv_group_add_obj(g_ui.input_group, btn_back);
  lv_group_focus_obj(btn_test);

  g_ui.current_page = PAGE_SETTINGS;
}

void ui_settings_handle_back(void) {
  ui_home_create();
}

void ui_settings_load_persisted(void) {
}

void ui_settings_sync_volume(uint8_t volume) {
  (void)volume;
}
