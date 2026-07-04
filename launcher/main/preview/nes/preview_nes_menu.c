#include "preview_nes_menu.h"

#include "hal_display.h"
#include "hal_input.h"
#include "platform_time.h"
#include <stdio.h>
#include <string.h>

#define FW 5
#define FH 7
#define FG 1
#define CHUNK_ROWS 24

static const uint8_t font5x7[][5] = {
    [0]  = {0x00, 0x00, 0x00, 0x00, 0x00},
    [1]  = {0x3E, 0x09, 0x09, 0x09, 0x3E},
    [2]  = {0x3F, 0x25, 0x25, 0x25, 0x1A},
    [3]  = {0x1E, 0x21, 0x21, 0x21, 0x12},
    [4]  = {0x3F, 0x21, 0x21, 0x21, 0x1E},
    [5]  = {0x3F, 0x25, 0x25, 0x25, 0x21},
    [6]  = {0x3F, 0x05, 0x05, 0x05, 0x01},
    [7]  = {0x1E, 0x21, 0x29, 0x29, 0x1A},
    [8]  = {0x3F, 0x04, 0x04, 0x04, 0x3F},
    [9]  = {0x21, 0x21, 0x3F, 0x21, 0x21},
    [10] = {0x18, 0x21, 0x21, 0x1F, 0x01},
    [11] = {0x3F, 0x04, 0x0A, 0x11, 0x21},
    [12] = {0x3F, 0x20, 0x20, 0x20, 0x20},
    [13] = {0x3F, 0x02, 0x04, 0x02, 0x3F},
    [14] = {0x3F, 0x02, 0x04, 0x08, 0x3F},
    [15] = {0x1E, 0x21, 0x21, 0x21, 0x1E},
    [16] = {0x3F, 0x09, 0x09, 0x09, 0x06},
    [17] = {0x1E, 0x21, 0x29, 0x11, 0x2E},
    [18] = {0x3F, 0x09, 0x09, 0x09, 0x26},
    [19] = {0x12, 0x25, 0x25, 0x25, 0x18},
    [20] = {0x01, 0x01, 0x3F, 0x01, 0x01},
    [21] = {0x1F, 0x20, 0x20, 0x20, 0x1F},
    [22] = {0x07, 0x18, 0x20, 0x18, 0x07},
    [23] = {0x3F, 0x10, 0x0C, 0x10, 0x3F},
    [24] = {0x21, 0x12, 0x0C, 0x12, 0x21},
    [25] = {0x01, 0x02, 0x3C, 0x02, 0x01},
    [26] = {0x21, 0x31, 0x29, 0x25, 0x23},
    [27] = {0x00, 0x04, 0x0E, 0x04, 0x00},
};

static inline int char_idx(char c) {
  if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
  if (c == ' ')            return 0;
  if (c == '>')            return 27;
  return 0;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

struct item {
  int  y;
  char text[32];
  int  sel;
};

static void draw_line(uint16_t *l, const struct item *its, int n,
                      int cy, uint16_t bg, uint16_t fg,
                      uint16_t sbg, uint16_t tfg) {
  for (int x = 0; x < 320; x++) l[x] = bg;
  for (int i = 0; i < n; i++) {
    int y  = its[i].y;
    int sel = its[i].sel;
    const char *s = its[i].text;
    if (cy < y || cy >= y + FH) continue;
    int row = cy - y;
    uint16_t ibg = sel ? sbg : bg;
    if (i == 0) ibg = bg;
    int cx = (i == 0) ? 12 : 30;
    while (*s) {
      int ci = char_idx(*s);
      const uint8_t *g = font5x7[ci];
      uint8_t col = g[row];
      for (int c = 0; c < FW; c++) {
        int px = cx + c;
        if (px >= 0 && px < 320)
          l[px] = (col & (1 << c)) ? ((i == 0) ? tfg : fg) : ibg;
      }
      cx += FW + FG;
      s++;
    }
  }
}

int nes_menu_show(void) {
  static uint16_t sl[320 * CHUNK_ROWS];
  static const char *lbl[] = {"Continue", "Save", "Load", "Reset", "Exit"};
  static const int lc = 5;
  static int sel = 0;

  uint16_t bg = rgb565(0, 0, 80);
  uint16_t fg = rgb565(220, 220, 255);
  uint16_t sbg = rgb565(30, 60, 140);
  uint16_t tfg = rgb565(255, 255, 100);

  input_gamepad_state pr;
  hal_input_read(&pr);

  for (;;) {
    struct item its[6];
    int n = 0;
    its[n].y = 8; its[n].sel = 0;
    snprintf(its[n].text, sizeof(its[n].text), "NES"); n++;
    int by = 45;
    for (int i = 0; i < lc; i++) {
      its[n].y = by + i * 32;
      its[n].sel = (i == sel);
      if (i == sel)
        snprintf(its[n].text, sizeof(its[n].text), ">%s", lbl[i]);
      else
        snprintf(its[n].text, sizeof(its[n].text), " %s", lbl[i]);
      n++;
    }

    for (int y0 = 0; y0 < 240; y0 += CHUNK_ROWS) {
      int rows = CHUNK_ROWS;
      if (y0 + rows > 240) rows = 240 - y0;

      for (int dy = 0; dy < rows; dy++)
        draw_line(sl + dy * 320, its, n, y0 + dy, bg, fg, sbg, tfg);

      hal_display_flush(0, y0, 319, y0 + rows - 1, (uint8_t *)sl);
    }

    platform_sleep_ms(50);

    input_gamepad_state st;
    hal_input_read(&st);

    if (st.values[GAMEPAD_INPUT_UP] && !pr.values[GAMEPAD_INPUT_UP]) {
      sel--; if (sel < 0) sel = lc - 1;
    }
    if (st.values[GAMEPAD_INPUT_DOWN] && !pr.values[GAMEPAD_INPUT_DOWN]) {
      sel++; if (sel >= lc) sel = 0;
    }
    if (st.values[GAMEPAD_INPUT_A] && !pr.values[GAMEPAD_INPUT_A]) return sel;
    if (st.values[GAMEPAD_INPUT_B] && !pr.values[GAMEPAD_INPUT_B]) return NES_MENU_CONTINUE;
    if (st.values[GAMEPAD_INPUT_MENU] && !pr.values[GAMEPAD_INPUT_MENU]) return NES_MENU_CONTINUE;

    pr = st;
  }
}
