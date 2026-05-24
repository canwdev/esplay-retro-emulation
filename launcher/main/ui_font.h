#pragma once

#include "lvgl.h"

/** Vonwaon Bitmap 12px — default UI font (CJK + Latin). */
const lv_font_t *ui_font_default(void);

/** Re-apply LVGL default theme using the Vonwaon font (call after lv_display_create). */
void ui_font_apply_display(lv_display_t *disp);
