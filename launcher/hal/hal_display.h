#pragma once

#include <stdint.h>

#define HAL_DISPLAY_WIDTH  320
#define HAL_DISPLAY_HEIGHT 240

void hal_display_init(void);
void hal_display_flush(int x1, int y1, int x2, int y2, uint8_t *rgb565);
void hal_display_flush_raw(int x1, int y1, int x2, int y2, uint8_t *rgb565);
void hal_display_set_brightness(uint8_t pct);

void *hal_display_panel_handle(void);

#ifdef TARGET_ESP32
#include "lcd.h"
void hal_display_set_panel(esp_lcd_panel_handle_t panel);
#endif
