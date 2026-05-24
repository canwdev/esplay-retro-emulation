#include "ui_font.h"

LV_FONT_DECLARE(ui_font_vonwaon);

const lv_font_t *ui_font_default(void) { return &ui_font_vonwaon; }

void ui_font_apply_display(lv_display_t *disp) {
#if LV_USE_THEME_DEFAULT
  if (lv_theme_default_is_inited())
    lv_theme_default_deinit();
  lv_display_set_theme(
      disp, lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                  lv_palette_main(LV_PALETTE_RED),
                                  LV_THEME_DEFAULT_DARK, ui_font_default()));
#endif
}
