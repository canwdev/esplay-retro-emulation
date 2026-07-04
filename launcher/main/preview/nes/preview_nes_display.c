#include "preview_nes_display.h"

#include "hal_display.h"
#include <string.h>

#define CHUNK_ROWS 24

static uint16_t s_pal[256];
static uint16_t s_chunk[320 * CHUNK_ROWS];

void nes_display_set_palette(const uint16_t *palette) {
  memcpy(s_pal, palette, sizeof(s_pal));
}

void nes_display_write(const uint8_t *data, int scale) {
  if (!data) {
    memset(s_chunk, 0, sizeof(s_chunk));
    for (int y0 = 0; y0 < 240; y0 += CHUNK_ROWS) {
      int rows = CHUNK_ROWS;
      if (y0 + rows > 240) rows = 240 - y0;
      hal_display_flush(0, y0, 319, y0 + rows - 1, (uint8_t *)s_chunk);
    }
    return;
  }

  if (scale == NES_SCALE_FILL) {
    int x_ratio = (int)(((uint32_t)NES_FRAME_WIDTH << 16) / 320) + 1;
    int y_ratio = (int)(((uint32_t)NES_FRAME_HEIGHT << 16) / 240) + 1;

    for (int y0 = 0; y0 < 240; y0 += CHUNK_ROWS) {
      int rows = CHUNK_ROWS;
      if (y0 + rows > 240) rows = 240 - y0;

      for (int dy = 0; dy < rows; dy++) {
        int y   = y0 + dy;
        int y2  = ((y * y_ratio) >> 16);
        if (y2 >= NES_FRAME_HEIGHT) y2 = NES_FRAME_HEIGHT - 1;
        const uint8_t *src = data + y2 * NES_FRAME_WIDTH;
        uint16_t *line = s_chunk + dy * 320;
        for (int x = 0; x < 320; x++) {
          int x2 = ((x * x_ratio) >> 16);
          if (x2 >= NES_FRAME_WIDTH) x2 = NES_FRAME_WIDTH - 1;
          line[x] = s_pal[src[x2]];
        }
      }
      hal_display_flush(0, y0, 319, y0 + rows - 1, (uint8_t *)s_chunk);
    }
  } else {
    int ow = NES_FRAME_WIDTH + (240 - NES_FRAME_HEIGHT);
    int ox = (320 - ow) / 2;
    int x_ratio = (int)(((uint32_t)NES_FRAME_WIDTH << 16) / ow) + 1;
    int y_ratio = (int)(((uint32_t)NES_FRAME_HEIGHT << 16) / 240) + 1;

    for (int y0 = 0; y0 < 240; y0 += CHUNK_ROWS) {
      int rows = CHUNK_ROWS;
      if (y0 + rows > 240) rows = 240 - y0;

      memset(s_chunk, 0, (size_t)rows * 320 * 2);

      for (int dy = 0; dy < rows; dy++) {
        int y   = y0 + dy;
        int y2  = ((y * y_ratio) >> 16);
        if (y2 >= NES_FRAME_HEIGHT) y2 = NES_FRAME_HEIGHT - 1;
        const uint8_t *src = data + y2 * NES_FRAME_WIDTH;
        uint16_t *line = s_chunk + dy * 320 + ox;
        for (int x = 0; x < ow; x++) {
          int x2 = ((x * x_ratio) >> 16);
          if (x2 >= NES_FRAME_WIDTH) x2 = NES_FRAME_WIDTH - 1;
          line[x] = s_pal[src[x2]];
        }
      }
      hal_display_flush(0, y0, 319, y0 + rows - 1, (uint8_t *)s_chunk);
    }
  }
}
