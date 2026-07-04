#pragma once

#include <stdint.h>

#define NES_FRAME_WIDTH  256
#define NES_FRAME_HEIGHT 224

enum {
  NES_SCALE_FILL = 0,
  NES_SCALE_FIT  = 1,
};

void nes_display_set_palette(const uint16_t *palette);
void nes_display_write(const uint8_t *data, int scale);
