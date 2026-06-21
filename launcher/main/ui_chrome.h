#pragma once

#include "lvgl.h"
#include <stdbool.h>

typedef struct {
  lv_obj_t *title_label;
  lv_obj_t *battery_label;
} ui_chrome_t;

/** Y offset where page body content should begin (below the chrome bar). */
lv_coord_t ui_chrome_body_top(void);

/** Create top bar: centered title + battery icon on the right. */
ui_chrome_t ui_chrome_create(lv_obj_t *parent, const char *title);

void ui_chrome_set_title(ui_chrome_t *chrome, const char *title);
void ui_chrome_set_music_active(bool active);

/** Call before destroying the parent screen/widgets. */
void ui_chrome_detach(ui_chrome_t *chrome);

/** Refresh battery icon on the active chrome (safe to call periodically). */
void ui_chrome_update_battery(void);
