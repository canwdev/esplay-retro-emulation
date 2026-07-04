#include "preview_nes_display.h"

#include "hal_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform_mem.h"
#include <string.h>

#define CHUNK_ROWS 24
#define NUM_CHUNKS 10
#define FB_BYTES   (NES_SCREEN_PITCH * NES_FRAME_HEIGHT)

static uint16_t  s_pal[256];
static uint16_t  s_chunk[320 * CHUNK_ROWS];
static uint32_t  s_chunk_hash[NUM_CHUNKS];

static uint8_t  *s_fb;
static int       s_fb_stride;

static SemaphoreHandle_t s_frame_ready;
static SemaphoreHandle_t s_disp_done;
static TaskHandle_t s_disp_task;
static volatile bool s_disp_running;
static int s_disp_scale;

static void display_task(void *arg);

static inline uint32_t hash32(const uint16_t *data, int words) {
  uint32_t h = 5381;
  for (int i = 0; i < words; i++)
    h = ((h << 5) + h) + data[i];
  return h;
}

void nes_display_init(void) {
  memset(s_pal, 0, sizeof(s_pal));
  memset(s_chunk_hash, 0, sizeof(s_chunk_hash));

  s_fb_stride = NES_SCREEN_PITCH;
  s_fb        = (uint8_t *)platform_malloc(FB_BYTES);
  if (!s_fb) return;
  memset(s_fb, 0, FB_BYTES);
  s_disp_scale = NES_SCALE_FILL;

  s_frame_ready  = xSemaphoreCreateBinary();
  s_disp_done    = xSemaphoreCreateBinary();
  s_disp_running = true;
  xSemaphoreGive(s_disp_done);

  xTaskCreatePinnedToCore(display_task, "nes_disp", 8192, NULL, 3,
                          &s_disp_task, 1);
}

void nes_display_deinit(void) {
  s_disp_running = false;
  xSemaphoreGive(s_frame_ready);
  vTaskDelay(pdMS_TO_TICKS(50));
  if (s_fb) { platform_free(s_fb); s_fb = NULL; }
}

void nes_display_set_palette(const uint16_t *palette) {
  memcpy(s_pal, palette, sizeof(s_pal));
}

void nes_display_submit(const uint8_t *data, int stride, int scale) {
  if (xSemaphoreTake(s_disp_done, 0) != pdTRUE)
    return;

  s_disp_scale = scale;
  memcpy(s_fb, data, FB_BYTES);
  xSemaphoreGive(s_frame_ready);
}

static void render_fill(const uint8_t *data, int stride) {
  int x_ratio = (int)(((uint32_t)NES_FRAME_WIDTH << 16) / 320) + 1;
  int y_ratio = (int)(((uint32_t)NES_FRAME_HEIGHT << 16) / 240) + 1;

  for (int y0 = 0, ci = 0; y0 < 240; y0 += CHUNK_ROWS, ci++) {
    for (int dy = 0; dy < CHUNK_ROWS; dy++) {
      int y  = y0 + dy;
      int y2 = ((y * y_ratio) >> 16);
      if (y2 >= NES_FRAME_HEIGHT) y2 = NES_FRAME_HEIGHT - 1;
      const uint8_t *src = data + y2 * stride;
      uint16_t *line = s_chunk + dy * 320;
      for (int x = 0; x < 320; x++) {
        int x2 = ((x * x_ratio) >> 16);
        if (x2 >= NES_FRAME_WIDTH) x2 = NES_FRAME_WIDTH - 1;
        line[x] = s_pal[src[x2]];
      }
    }

    uint32_t h = hash32(s_chunk, CHUNK_ROWS * 320);
    if (h != s_chunk_hash[ci]) {
      hal_display_flush(0, y0, 319, y0 + CHUNK_ROWS - 1, (uint8_t *)s_chunk);
      s_chunk_hash[ci] = h;
    }
  }
}

static void render_fit(const uint8_t *data, int stride) {
  int ow = NES_FRAME_WIDTH + (240 - NES_FRAME_HEIGHT);
  int ox = (320 - ow) / 2;
  int x_ratio = (int)(((uint32_t)NES_FRAME_WIDTH << 16) / ow) + 1;
  int y_ratio = (int)(((uint32_t)NES_FRAME_HEIGHT << 16) / 240) + 1;

  for (int y0 = 0, ci = 0; y0 < 240; y0 += CHUNK_ROWS, ci++) {
    memset(s_chunk, 0, (size_t)CHUNK_ROWS * 320 * 2);

    for (int dy = 0; dy < CHUNK_ROWS; dy++) {
      int y  = y0 + dy;
      int y2 = ((y * y_ratio) >> 16);
      if (y2 >= NES_FRAME_HEIGHT) y2 = NES_FRAME_HEIGHT - 1;
      const uint8_t *src = data + y2 * stride;
      uint16_t *line = s_chunk + dy * 320 + ox;
      for (int x = 0; x < ow; x++) {
        int x2 = ((x * x_ratio) >> 16);
        if (x2 >= NES_FRAME_WIDTH) x2 = NES_FRAME_WIDTH - 1;
        line[x] = s_pal[src[x2]];
      }
    }

    uint32_t h = hash32(s_chunk, CHUNK_ROWS * 320);
    if (h != s_chunk_hash[ci]) {
      hal_display_flush(0, y0, 319, y0 + CHUNK_ROWS - 1, (uint8_t *)s_chunk);
      s_chunk_hash[ci] = h;
    }
  }
}

static void display_task(void *arg) {
  (void)arg;
  while (s_disp_running) {
    if (xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(100)) != pdTRUE)
      continue;

    if (!s_disp_running) break;

    const uint8_t *data = s_fb + NES_SCREEN_OVERDRAW;

    if (s_disp_scale == NES_SCALE_FILL)
      render_fill(data, s_fb_stride);
    else
      render_fit(data, s_fb_stride);

    xSemaphoreGive(s_disp_done);
  }
  vTaskDelete(NULL);
}
