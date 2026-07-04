#include "hal_display.h"

#include <SDL.h>
#include <stdbool.h>
#include <string.h>

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static uint16_t *s_fb;
static bool s_inited;
static uint8_t s_brightness = 100;

void hal_display_init(void) {
  if (s_inited)
    return;

  int win_w = HAL_DISPLAY_WIDTH * 2;
  int win_h = HAL_DISPLAY_HEIGHT * 2;
  s_window =
      SDL_CreateWindow("ESPlay Launcher", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, win_w, win_h, SDL_WINDOW_SHOWN);
  if (!s_window)
    return;

  s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
  if (!s_renderer)
    return;

  SDL_RenderSetLogicalSize(s_renderer, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT);

  s_texture =
      SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                        SDL_TEXTUREACCESS_STREAMING, HAL_DISPLAY_WIDTH,
                        HAL_DISPLAY_HEIGHT);
  if (!s_texture)
    return;

  s_fb = (uint16_t *)SDL_malloc(HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT * 2);
  if (!s_fb)
    return;
  memset(s_fb, 0, HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT * 2);

  s_inited = true;
}

void hal_display_flush(int x1, int y1, int x2, int y2, uint8_t *rgb565) {
  if (!s_inited || !rgb565 || !s_fb)
    return;

  if (x1 < 0)
    x1 = 0;
  if (y1 < 0)
    y1 = 0;
  if (x2 >= HAL_DISPLAY_WIDTH)
    x2 = HAL_DISPLAY_WIDTH - 1;
  if (y2 >= HAL_DISPLAY_HEIGHT)
    y2 = HAL_DISPLAY_HEIGHT - 1;
  if (x2 < x1 || y2 < y1)
    return;

  int w = x2 - x1 + 1;
  const uint16_t *src = (const uint16_t *)rgb565;
  for (int y = y1; y <= y2; y++) {
    memcpy(&s_fb[y * HAL_DISPLAY_WIDTH + x1], src, (size_t)w * 2);
    src += w;
  }

  SDL_UpdateTexture(s_texture, NULL, s_fb, HAL_DISPLAY_WIDTH * 2);
  uint8_t c = (uint8_t)((uint16_t)s_brightness * 255U / 100U);
  SDL_SetTextureColorMod(s_texture, c, c, c);
  SDL_RenderClear(s_renderer);
  SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
  SDL_RenderPresent(s_renderer);
}

void hal_display_set_brightness(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  s_brightness = pct;
}

void hal_display_flush_raw(int x1, int y1, int x2, int y2, uint8_t *rgb565) {
  hal_display_flush(x1, y1, x2, y2, rgb565);
}
