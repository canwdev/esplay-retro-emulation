#include "ui_screen_test.h"
#include "ui_app.h"
#include "ui_settings.h"
#include "lvgl.h"

typedef enum {
  TEST_SOLID_RED = 0,
  TEST_SOLID_GREEN,
  TEST_SOLID_BLUE,
  TEST_SOLID_WHITE,
  TEST_SOLID_BLACK,
  TEST_GRAD_H_RGB,
  TEST_GRAD_V_GRAY,
  TEST_GRAD_H_WARM,
  TEST_GAMUT_FULL,
  TEST_COUNT,
} screen_test_mode_t;

static lv_obj_t *s_content;
static lv_obj_t *s_label;
static lv_obj_t *s_hint;
static screen_test_mode_t s_mode;
static bool s_active;

static const char *screen_test_mode_name(screen_test_mode_t mode) {
  switch (mode) {
  case TEST_SOLID_RED:
    return "Solid Red";
  case TEST_SOLID_GREEN:
    return "Solid Green";
  case TEST_SOLID_BLUE:
    return "Solid Blue";
  case TEST_SOLID_WHITE:
    return "Solid White";
  case TEST_SOLID_BLACK:
    return "Solid Black";
  case TEST_GRAD_H_RGB:
    return "Gradient H RGB";
  case TEST_GRAD_V_GRAY:
    return "Gradient V Gray";
  case TEST_GRAD_H_WARM:
    return "Gradient H Warm";
  case TEST_GAMUT_FULL:
    return "Full Gamut";
  default:
    return "Test";
  }
}

static lv_obj_t *screen_test_add_fill(lv_color_t color, bool gradient,
                                      lv_color_t grad_color,
                                      lv_grad_dir_t grad_dir) {
  lv_obj_t *fill = lv_obj_create(s_content);
  lv_obj_remove_style_all(fill);
  lv_obj_set_size(fill, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  if (gradient) {
    lv_obj_set_style_bg_grad_dir(fill, grad_dir, 0);
    lv_obj_set_style_bg_color(fill, color, 0);
    lv_obj_set_style_bg_grad_color(fill, grad_color, 0);
  } else {
    lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_color(fill, color, 0);
  }
  return fill;
}

static lv_obj_t *screen_test_add_row(lv_obj_t *parent, lv_coord_t height) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, height);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_radius(row, 0, 0);
  return row;
}

static void screen_test_add_strip(lv_obj_t *row, lv_color_t color) {
  lv_obj_t *strip = lv_obj_create(row);
  lv_obj_remove_style_all(strip);
  lv_obj_set_flex_grow(strip, 1);
  lv_obj_set_height(strip, LV_PCT(100));
  lv_obj_set_style_bg_color(strip, color, 0);
  lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(strip, 0, 0);
  lv_obj_set_style_border_width(strip, 0, 0);
}

static void screen_test_build_gamut(void) {
  lv_obj_t *root = lv_obj_create(s_content);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_border_width(root, 0, 0);

  lv_obj_t *hue_row = screen_test_add_row(root, 120);
  for (int i = 0; i < 36; i++) {
    uint16_t hue = (uint16_t)(i * 360 / 36);
    screen_test_add_strip(hue_row, lv_color_hsv_to_rgb(hue, 255, 255));
  }

  static const uint32_t bars[] = {0xFF0000, 0x00FF00, 0x0000FF, 0x00FFFF,
                                  0xFF00FF, 0xFFFF00, 0xFFFFFF};
  lv_obj_t *bar_row = screen_test_add_row(root, 56);
  for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); i++)
    screen_test_add_strip(bar_row, lv_color_hex(bars[i]));

  lv_obj_t *sat_row = screen_test_add_row(root, 56);
  for (int i = 0; i < 16; i++) {
    uint8_t sat = (uint8_t)(i * 255 / 15);
    screen_test_add_strip(sat_row, lv_color_hsv_to_rgb(200, sat, 255));
  }

  lv_obj_t *val_row = screen_test_add_row(root, 56);
  for (int i = 0; i < 16; i++) {
    uint8_t val = (uint8_t)(i * 255 / 15);
    screen_test_add_strip(val_row, lv_color_hsv_to_rgb(0, 0, val));
  }
}

static void screen_test_apply_mode(void) {
  if (!s_content)
    return;

  lv_obj_clean(s_content);

  switch (s_mode) {
  case TEST_SOLID_RED:
    screen_test_add_fill(lv_color_hex(0xFF0000), false, lv_color_black(),
                         LV_GRAD_DIR_NONE);
    break;
  case TEST_SOLID_GREEN:
    screen_test_add_fill(lv_color_hex(0x00FF00), false, lv_color_black(),
                         LV_GRAD_DIR_NONE);
    break;
  case TEST_SOLID_BLUE:
    screen_test_add_fill(lv_color_hex(0x0000FF), false, lv_color_black(),
                         LV_GRAD_DIR_NONE);
    break;
  case TEST_SOLID_WHITE:
    screen_test_add_fill(lv_color_hex(0xFFFFFF), false, lv_color_black(),
                         LV_GRAD_DIR_NONE);
    break;
  case TEST_SOLID_BLACK:
    screen_test_add_fill(lv_color_hex(0x000000), false, lv_color_black(),
                         LV_GRAD_DIR_NONE);
    break;
  case TEST_GRAD_H_RGB:
    screen_test_add_fill(lv_color_hex(0xFF0000), true, lv_color_hex(0x0000FF),
                         LV_GRAD_DIR_HOR);
    break;
  case TEST_GRAD_V_GRAY:
    screen_test_add_fill(lv_color_hex(0x000000), true, lv_color_hex(0xFFFFFF),
                         LV_GRAD_DIR_VER);
    break;
  case TEST_GRAD_H_WARM:
    screen_test_add_fill(lv_color_hex(0xFFFF00), true, lv_color_hex(0xFF0066),
                         LV_GRAD_DIR_HOR);
    break;
  case TEST_GAMUT_FULL:
    screen_test_build_gamut();
    break;
  default:
    break;
  }

  if (s_label)
    lv_label_set_text_fmt(s_label, "%s  %d/%d", screen_test_mode_name(s_mode),
                          (int)s_mode + 1, TEST_COUNT);
}

static void screen_test_set_overlay_style(bool dark_bar) {
  lv_color_t bar =
      dark_bar ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
  lv_color_t text =
      dark_bar ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x111111);
  lv_opa_t bar_opa = dark_bar ? LV_OPA_60 : LV_OPA_50;

  if (s_label) {
    lv_obj_set_style_bg_color(s_label, bar, 0);
    lv_obj_set_style_bg_opa(s_label, bar_opa, 0);
    lv_obj_set_style_text_color(s_label, text, 0);
  }
  if (s_hint) {
    lv_obj_set_style_bg_color(s_hint, bar, 0);
    lv_obj_set_style_bg_opa(s_hint, bar_opa, 0);
    lv_obj_set_style_text_color(s_hint, text, 0);
  }
}

static void screen_test_update_overlay(void) {
  screen_test_set_overlay_style(true);
}

void ui_screen_test_open(void) {
  if (!g_ui.screen)
    return;

  s_mode = TEST_SOLID_RED;
  s_active = true;

  lv_group_remove_all_objs(g_ui.input_group);
  lv_obj_clean(g_ui.screen);

  lv_obj_set_style_bg_opa(g_ui.screen, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(g_ui.screen, lv_color_black(), 0);
  lv_obj_set_style_border_width(g_ui.screen, 0, 0);
  lv_obj_set_style_pad_all(g_ui.screen, 0, 0);

  s_content = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(s_content);
  lv_obj_set_size(s_content, LV_PCT(100), LV_PCT(100));
  lv_obj_center(s_content);
  lv_obj_set_style_pad_all(s_content, 0, 0);
  lv_obj_set_style_border_width(s_content, 0, 0);

  s_label = lv_label_create(g_ui.screen);
  lv_obj_set_width(s_label, 300);
  lv_label_set_long_mode(s_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_pad_all(s_label, 4, 0);
  lv_obj_set_style_radius(s_label, 2, 0);
  lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, 4);

  s_hint = lv_label_create(g_ui.screen);
  lv_label_set_text(s_hint,
                    LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " pattern   B back");
  lv_obj_set_style_pad_all(s_hint, 4, 0);
  lv_obj_set_style_radius(s_hint, 2, 0);
  lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -4);

  screen_test_update_overlay();
  screen_test_apply_mode();
  g_ui.current_page = PAGE_SCREEN_TEST;
}

void ui_screen_test_close(void) {
  s_content = NULL;
  s_label = NULL;
  s_hint = NULL;
  s_active = false;
  ui_settings_create();
}

bool ui_screen_test_is_active(void) { return s_active; }

void ui_screen_test_on_key(bool left, bool right) {
  if (!s_active)
    return;

  if (right)
    s_mode = (screen_test_mode_t)((s_mode + 1) % TEST_COUNT);
  else if (left)
    s_mode = (screen_test_mode_t)((s_mode + TEST_COUNT - 1) % TEST_COUNT);
  else
    return;

  screen_test_apply_mode();
  screen_test_update_overlay();
}
