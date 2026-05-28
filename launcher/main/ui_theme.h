#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"
#include <stdbool.h>

#define UI_THEME_COUNT 16

typedef enum {
  UI_THEME_DARK = 0,
  UI_THEME_LIGHT,
  UI_THEME_RED_DARK,
  UI_THEME_RED_LIGHT,
  UI_THEME_YELLOW_DARK,
  UI_THEME_YELLOW_LIGHT,
  UI_THEME_PURPLE_DARK,
  UI_THEME_PURPLE_LIGHT,
  UI_THEME_BLUE_DARK,
  UI_THEME_BLUE_LIGHT,
} ui_theme_id_t;

void ui_theme_init(void);
void ui_theme_set(int theme_id);
int ui_theme_get(void);
const char *ui_theme_name(int theme_id);
bool ui_theme_is_light(void);
void ui_theme_apply_screen(lv_obj_t *screen);
void ui_theme_style_label_primary(lv_obj_t *label);
void ui_theme_style_label_secondary(lv_obj_t *label);
void ui_theme_style_label_accent(lv_obj_t *label);
void ui_theme_style_label_truncated(lv_obj_t *label, lv_coord_t width);
void ui_theme_style_label_row(lv_obj_t *label, lv_coord_t row_h);
void ui_theme_style_btn(lv_obj_t *btn);
void ui_theme_style_list(lv_obj_t *list);
void ui_theme_style_list_btn(lv_obj_t *btn);
void ui_theme_style_msgbox(lv_obj_t *mbox);
void ui_theme_apply_msgbox(lv_obj_t *mbox);
void ui_theme_style_bar(lv_obj_t *bar);
void ui_theme_style_panel(lv_obj_t *obj);
void ui_theme_style_scroll(lv_obj_t *obj);
lv_color_t ui_theme_color_bg(void);
lv_color_t ui_theme_color_accent(void);
lv_color_t ui_theme_color_text(void);
lv_color_t ui_theme_color_text_dim(void);
lv_color_t ui_theme_color_panel(void);
lv_color_t ui_theme_color_focus_bg(void);

#endif
