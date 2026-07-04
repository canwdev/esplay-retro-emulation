#pragma once

#include <stdint.h>

#define NES_FRAME_WIDTH  256
#define NES_FRAME_HEIGHT 224

#define NES_SCREEN_OVERDRAW 8
#define NES_SCREEN_PITCH    (8 + 256 + 8)

enum {
    NES_SCALE_FILL = 0,
    NES_SCALE_FIT  = 1,
};

void nes_display_init(void);
void nes_display_deinit(void);
void nes_display_submit(const uint8_t *data, int stride, int scale);
void nes_display_set_palette(const uint16_t *palette);
