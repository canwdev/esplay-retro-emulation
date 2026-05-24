#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "power.h"
#include "lvgl.h"

#define UI_CHROME_BAR_H    16
#define UI_CHROME_GAP      2
#define UI_CHROME_SIDE_W   28

static lv_obj_t *s_active_battery;

static void ui_chrome_battery_symbol(battery_state *bat, char *out, size_t out_sz) {
  const char *sym;
  if (bat->state == FULL_CHARGED || bat->state == CHARGING)
    sym = LV_SYMBOL_CHARGE;
  else if (bat->percentage > 75)
    sym = LV_SYMBOL_BATTERY_FULL;
  else if (bat->percentage > 50)
    sym = LV_SYMBOL_BATTERY_3;
  else if (bat->percentage > 25)
    sym = LV_SYMBOL_BATTERY_2;
  else if (bat->percentage > 5)
    sym = LV_SYMBOL_BATTERY_1;
  else
    sym = LV_SYMBOL_BATTERY_EMPTY;

  lv_snprintf(out, out_sz, "%s", sym);
}

lv_coord_t ui_chrome_body_top(void) {
  return UI_CHROME_BAR_H + UI_CHROME_GAP;
}

ui_chrome_t ui_chrome_create(lv_obj_t *parent, const char *title) {
  ui_chrome_t chrome = {0};

  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, LV_PCT(100), UI_CHROME_BAR_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(bar, 6, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);

  lv_obj_t *left = lv_obj_create(bar);
  lv_obj_remove_style_all(left);
  lv_obj_set_size(left, UI_CHROME_SIDE_W, UI_CHROME_BAR_H);
  lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(left, 0, 0);

  chrome.title_label = lv_label_create(bar);
  lv_label_set_text(chrome.title_label, title ? title : "");
  ui_theme_style_label_accent(chrome.title_label);
  lv_obj_set_flex_grow(chrome.title_label, 1);
  lv_obj_set_width(chrome.title_label, 0);
  lv_label_set_long_mode(chrome.title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(chrome.title_label, LV_TEXT_ALIGN_CENTER, 0);

  chrome.battery_label = lv_label_create(bar);
  lv_obj_set_width(chrome.battery_label, UI_CHROME_SIDE_W);
  lv_obj_set_style_text_align(chrome.battery_label, LV_TEXT_ALIGN_RIGHT, 0);
  ui_theme_style_label_secondary(chrome.battery_label);
  lv_obj_set_style_text_font(chrome.battery_label, ui_font_builtin(), 0);

  s_active_battery = chrome.battery_label;
  ui_chrome_update_battery();

  return chrome;
}

void ui_chrome_set_title(ui_chrome_t *chrome, const char *title) {
  if (!chrome || !chrome->title_label || !lv_obj_is_valid(chrome->title_label))
    return;
  lv_label_set_text(chrome->title_label, title ? title : "");
}

void ui_chrome_detach(ui_chrome_t *chrome) {
  if (chrome && chrome->battery_label == s_active_battery)
    s_active_battery = NULL;
  if (chrome) {
    chrome->title_label   = NULL;
    chrome->battery_label = NULL;
  }
}

void ui_chrome_update_battery(void) {
  if (!s_active_battery || !lv_obj_is_valid(s_active_battery))
    return;

  battery_state bat;
  battery_level_read(&bat);

  char sym[8];
  ui_chrome_battery_symbol(&bat, sym, sizeof(sym));
  lv_label_set_text(s_active_battery, sym);
}
