#pragma once

#include <stdint.h>

#define HAL_DISPLAY_WIDTH  320
#define HAL_DISPLAY_HEIGHT 240

void hal_display_init(void);
void hal_display_flush(int x1, int y1, int x2, int y2, uint8_t *rgb565);
void hal_display_set_brightness(uint8_t pct);
