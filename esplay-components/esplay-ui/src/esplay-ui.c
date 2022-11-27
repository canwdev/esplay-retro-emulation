#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include <stdio.h>
#include "ugui.h"
#include "display.h"
#include "ugui.h"
#include "sdcard.h"
#include "gamepad.h"
#include "esplay-ui.h"

#define MAX_CHR (320 / 9)
#define CONTAIN_HEIGHT  192 //192=LCD Height (240) - header(16) - footer(16) - char height (16)
#define MAX_ITEM (CONTAIN_HEIGHT / 16) // 193 = LCD Height (240) - header(16) - footer(16) - char height (15)

uint16_t *fb;
static UG_GUI *ugui;

static void pset(UG_S16 x, UG_S16 y, UG_COLOR color)
{
  fb[y * 320 + x] = color;
}

uint16_t * ui_get_fb()
{
  return fb;
}

void ui_clear_screen()
{
  memset(fb, 0, 320 * 240 * 2);
}

void ui_flush()
{
  write_frame_rectangleLE(0, 0, 320, 240, fb);
}

void ui_init()
{
  fb = malloc(320 * 240 * sizeof(uint16_t));
  ugui = malloc(sizeof(UG_GUI));
  UG_Init(ugui, pset, 320, 240);
  UG_FontSelect(&FONT_8X12);
  ui_clear_screen();
  ui_flush();
}

void ui_deinit()
{
  free(fb);
  free(ugui);
  fb = NULL;
  ugui = NULL;
}

