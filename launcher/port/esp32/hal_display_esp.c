#include "hal_display.h"

#include <stdbool.h>

#include "lcd.h"
#include "lvgl.h"

static esp_lcd_panel_handle_t s_panel;
static bool s_inited = false;

void hal_display_init(void) {
  if (s_inited)
    return;
  lcd_init(&s_panel);
  s_inited = true;
}

void hal_display_flush(int x1, int y1, int x2, int y2, uint8_t *rgb565) {
  if (!s_inited)
    return;
  lv_draw_sw_rgb565_swap(rgb565, (uint32_t)((x2 - x1 + 1) * (y2 - y1 + 1)));
  lcd_draw(s_panel, x1, y1, x2 + 1, y2 + 1, rgb565);
}

void hal_display_flush_raw(int x1, int y1, int x2, int y2, uint8_t *rgb565) {
  if (!s_inited)
    return;
  lcd_draw(s_panel, x1, y1, x2 + 1, y2 + 1, rgb565);
}

void hal_display_set_brightness(uint8_t pct) {
  lcd_set_brightness(pct);
}

void hal_display_set_panel(esp_lcd_panel_handle_t panel) {
  s_panel = panel;
  s_inited = true;
}
