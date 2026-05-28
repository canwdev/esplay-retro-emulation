#include "preview_bmp.h"

#include "file_manager.h"
#include "hal_display.h"
#include "platform_log.h"
#include "ui_backlight.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "preview_bmp";

#define BMP_MIN_SCALE 16U
#define BMP_MAX_SCALE 1024U

typedef struct {
  int32_t width;
  int32_t height;
  uint16_t bpp;
  uint32_t compression;
  uint32_t data_offset;
  uint32_t row_size_bytes;
  bool top_down;
} bmp_meta_t;

typedef struct {
  char path[FM_PATH_MAX];
  bmp_meta_t meta;
  FILE *file;
  uint8_t *row_buf;
  lv_draw_buf_t *canvas_draw_buf;
  uint32_t scale;
  uint32_t fit_scale;
  int32_t pan_x;
  int32_t pan_y;
  lv_coord_t viewport_w;
  lv_coord_t viewport_h;
  int32_t cached_row;
} bmp_state_t;

static bmp_state_t s_state;
static ui_chrome_t s_chrome;
static lv_obj_t *s_viewport;
static lv_obj_t *s_canvas;
static lv_obj_t *s_status_label;
static bool s_active;

static uint16_t bmp_u16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t bmp_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int32_t bmp_s32(const uint8_t *p) {
  return (int32_t)bmp_u32(p);
}

static FILE *bmp_fopen_native(const char *path, const char *mode) {
#ifdef TARGET_SIM
  unsigned long winerr = 0;
  FILE *f = sim_fopen_utf8(path, mode, &winerr);
  if (!f) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "fopen failed: %s (winerr=%lu)", path,
                 winerr);
  }
  return f;
#else
  FILE *f = fopen(path, mode);
  if (!f)
    platform_log(PLATFORM_LOG_ERROR, TAG, "fopen failed: %s", path);
  return f;
#endif
}

static bool bmp_load_meta(FILE *f, const char *path, bmp_meta_t *meta) {
  uint8_t header[54];
  if (fseek(f, 0, SEEK_SET) != 0)
    return false;
  size_t n = fread(header, 1, sizeof(header), f);
  if (n != sizeof(header)) {
    platform_log(PLATFORM_LOG_WARN, TAG, "short bmp header: %s", path);
    return false;
  }

  if (header[0] != 'B' || header[1] != 'M') {
    platform_log(PLATFORM_LOG_WARN, TAG, "not a bmp file: %s", path);
    return false;
  }

  uint32_t dib_size = bmp_u32(&header[14]);
  int32_t width = bmp_s32(&header[18]);
  int32_t height = bmp_s32(&header[22]);
  uint16_t planes = bmp_u16(&header[26]);
  uint16_t bpp = bmp_u16(&header[28]);
  uint32_t compression = bmp_u32(&header[30]);
  uint32_t data_offset = bmp_u32(&header[10]);
  bool top_down = false;

  if (height < 0) {
    height = -height;
    top_down = true;
  }

  if (dib_size < 40 || width <= 0 || height <= 0 || planes != 1 ||
      data_offset < 54) {
    platform_log(PLATFORM_LOG_WARN, TAG, "unsupported bmp header: %s", path);
    return false;
  }
  if (compression != 0) {
    platform_log(PLATFORM_LOG_WARN, TAG,
                 "unsupported bmp compression=%lu: %s",
                 (unsigned long)compression, path);
    return false;
  }
  if (bpp != 16 && bpp != 24 && bpp != 32) {
    platform_log(PLATFORM_LOG_WARN, TAG, "unsupported bmp bpp=%u: %s",
                 (unsigned)bpp, path);
    return false;
  }

  meta->width = width;
  meta->height = height;
  meta->bpp = bpp;
  meta->compression = compression;
  meta->data_offset = data_offset;
  meta->row_size_bytes = ((uint32_t)bpp * (uint32_t)width + 31U) / 32U * 4U;
  meta->top_down = top_down;
  return true;
}

static uint32_t bmp_fit_scale(const bmp_meta_t *meta, lv_coord_t max_w,
                              lv_coord_t max_h) {
  uint32_t sx = (uint32_t)(((uint64_t)max_w * LV_SCALE_NONE) / meta->width);
  uint32_t sy = (uint32_t)(((uint64_t)max_h * LV_SCALE_NONE) / meta->height);
  uint32_t scale = LV_MIN(sx, sy);
  if (scale == 0)
    scale = 1;
  if (scale > LV_SCALE_NONE)
    scale = LV_SCALE_NONE;
  return scale;
}

static int32_t bmp_scaled_dim(int32_t src, uint32_t scale) {
  return (int32_t)(((int64_t)src * (int64_t)scale + (LV_SCALE_NONE - 1)) /
                   LV_SCALE_NONE);
}

static lv_color_t bmp_decode_pixel_16(const uint8_t *src) {
  uint16_t v = bmp_u16(src);
  uint8_t r = (uint8_t)(((v >> 11) & 0x1F) << 3);
  uint8_t g = (uint8_t)(((v >> 5) & 0x3F) << 2);
  uint8_t b = (uint8_t)((v & 0x1F) << 3);
  return lv_color_make(r, g, b);
}

static lv_color_t bmp_decode_pixel(const uint8_t *src, uint16_t bpp) {
  if (bpp == 16)
    return bmp_decode_pixel_16(src);
  if (bpp == 24)
    return lv_color_make(src[2], src[1], src[0]);
  return lv_color_make(src[2], src[1], src[0]);
}

static void bmp_release_doc(void) {
  if (s_state.file) {
    fclose(s_state.file);
    s_state.file = NULL;
  }
  free(s_state.row_buf);
  s_state.row_buf = NULL;
}

static void bmp_release_canvas(void) {
  if (s_state.canvas_draw_buf) {
    lv_draw_buf_destroy(s_state.canvas_draw_buf);
    s_state.canvas_draw_buf = NULL;
  }
  s_canvas = NULL;
}

static bool bmp_load_row(int32_t src_y) {
  long pos;
  int32_t file_row;

  if (!s_state.file || !s_state.row_buf)
    return false;
  if (src_y < 0 || src_y >= s_state.meta.height)
    return false;
  if (s_state.cached_row == src_y)
    return true;

  file_row = s_state.meta.top_down ? src_y : (s_state.meta.height - 1 - src_y);
  pos = (long)(s_state.meta.data_offset +
               s_state.meta.row_size_bytes * (uint32_t)file_row);
  if (fseek(s_state.file, pos, SEEK_SET) != 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "row seek failed y=%d", (int)src_y);
    return false;
  }
  if (fread(s_state.row_buf, 1, s_state.meta.row_size_bytes, s_state.file) !=
      s_state.meta.row_size_bytes) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "row read failed y=%d", (int)src_y);
    return false;
  }

  s_state.cached_row = src_y;
  return true;
}

static void bmp_clamp_pan(int32_t draw_w, int32_t draw_h) {
  int32_t max_pan_x = 0;
  int32_t max_pan_y = 0;
  if (draw_w > s_state.viewport_w)
    max_pan_x = (draw_w - s_state.viewport_w) / 2;
  if (draw_h > s_state.viewport_h)
    max_pan_y = (draw_h - s_state.viewport_h) / 2;

  if (s_state.pan_x > max_pan_x)
    s_state.pan_x = max_pan_x;
  if (s_state.pan_x < -max_pan_x)
    s_state.pan_x = -max_pan_x;
  if (s_state.pan_y > max_pan_y)
    s_state.pan_y = max_pan_y;
  if (s_state.pan_y < -max_pan_y)
    s_state.pan_y = -max_pan_y;
}

static void bmp_update_status(void) {
  if (!s_status_label)
    return;
  lv_label_set_text_fmt(s_status_label, "%dx%d  %ubpp  %u%%",
                        (int)s_state.meta.width, (int)s_state.meta.height,
                        (unsigned)s_state.meta.bpp,
                        (unsigned)(s_state.scale * 100U / LV_SCALE_NONE));
}

static bool bmp_render_canvas(void) {
  int32_t img_x;
  int32_t img_y;
  int32_t draw_w = bmp_scaled_dim(s_state.meta.width, s_state.scale);
  int32_t draw_h = bmp_scaled_dim(s_state.meta.height, s_state.scale);
  int32_t x1;
  int32_t y1;
  int32_t x2;
  int32_t y2;
  int32_t y;
  uint16_t bg565 = lv_color_to_u16(lv_color_make(0, 0, 0));

  if (!s_canvas || !s_state.canvas_draw_buf)
    return false;
  if (draw_w < 1)
    draw_w = 1;
  if (draw_h < 1)
    draw_h = 1;

  bmp_clamp_pan(draw_w, draw_h);
  img_x = (s_state.viewport_w - draw_w) / 2 + s_state.pan_x;
  img_y = (s_state.viewport_h - draw_h) / 2 + s_state.pan_y;

  x1 = LV_MAX(0, img_x);
  y1 = LV_MAX(0, img_y);
  x2 = LV_MIN(s_state.viewport_w, img_x + draw_w);
  y2 = LV_MIN(s_state.viewport_h, img_y + draw_h);

  lv_draw_buf_clear(s_state.canvas_draw_buf, NULL);
  s_state.cached_row = -1;
  for (y = y1; y < y2; ++y) {
    int32_t src_y = ((int64_t)(y - img_y) * s_state.meta.height) / draw_h;
    uint16_t *dst = (uint16_t *)lv_draw_buf_goto_xy(s_state.canvas_draw_buf, 0, y);
    int32_t x;

    if (src_y < 0)
      src_y = 0;
    if (src_y >= s_state.meta.height)
      src_y = s_state.meta.height - 1;
    if (!bmp_load_row(src_y))
      return false;

    for (x = x1; x < x2; ++x) {
      int32_t src_x = ((int64_t)(x - img_x) * s_state.meta.width) / draw_w;
      const uint8_t *src;

      if (src_x < 0)
        src_x = 0;
      if (src_x >= s_state.meta.width)
        src_x = s_state.meta.width - 1;
      src = s_state.row_buf + src_x * (s_state.meta.bpp / 8);
      dst[x] = lv_color_to_u16(bmp_decode_pixel(src, s_state.meta.bpp));
    }

    for (x = 0; x < x1; ++x) {
      dst[x] = bg565;
    }
    for (x = x2; x < s_state.viewport_w; ++x) {
      dst[x] = bg565;
    }
  }

  bmp_update_status();
  lv_draw_buf_flush_cache(s_state.canvas_draw_buf, NULL);
  lv_obj_invalidate(s_canvas);
  return true;
}

static void bmp_apply_transform(void) {
  if (!bmp_render_canvas()) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "render failed: %s", s_state.path);
  }
}

static uint32_t bmp_zoom_next(uint32_t scale, bool zoom_in) {
  if (zoom_in) {
    uint32_t next = (scale >= 256) ? (scale * 5U + 3U) / 4U : scale + 32U;
    if (next <= scale)
      next = scale + 1;
    return next > BMP_MAX_SCALE ? BMP_MAX_SCALE : next;
  }

  if (scale <= BMP_MIN_SCALE)
    return BMP_MIN_SCALE;
  if (scale > 256) {
    uint32_t next = (scale * 4U) / 5U;
    return next < BMP_MIN_SCALE ? BMP_MIN_SCALE : next;
  }
  return scale > 32U ? scale - 32U : BMP_MIN_SCALE;
}

static void bmp_pan_by(int32_t dx, int32_t dy) {
  s_state.pan_x += dx;
  s_state.pan_y += dy;
  bmp_apply_transform();
}

static void bmp_reset_view(bool fit_to_screen) {
  s_state.scale = fit_to_screen ? s_state.fit_scale : LV_SCALE_NONE;
  s_state.pan_x = 0;
  s_state.pan_y = 0;
  bmp_apply_transform();
}

static bool preview_bmp_can_open(const char *path) {
  const char *dot = strrchr(path, '.');
  return dot && strcasecmp(dot, ".bmp") == 0;
}

static bool preview_bmp_open(const char *path, preview_open_args_t *args) {
  lv_coord_t top = ui_chrome_body_top() + 2;
  lv_coord_t bottom_reserved = 20;
  lv_coord_t viewport_w = HAL_DISPLAY_WIDTH - 12;
  lv_coord_t viewport_h = HAL_DISPLAY_HEIGHT - top - bottom_reserved;
  bmp_meta_t meta;
  FILE *file;

  file = bmp_fopen_native(path, "rb");
  if (!file)
    return false;
  if (!bmp_load_meta(file, path, &meta)) {
    fclose(file);
    return false;
  }

  memset(&s_state, 0, sizeof(s_state));
  s_state.file = file;
  s_state.row_buf = (uint8_t *)malloc(meta.row_size_bytes);
  s_state.canvas_draw_buf =
      lv_draw_buf_create((uint32_t)viewport_w, (uint32_t)viewport_h,
                         LV_COLOR_FORMAT_NATIVE, LV_STRIDE_AUTO);
  if (!s_state.row_buf || !s_state.canvas_draw_buf) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "alloc failed row=%lu canvas=%lu",
                 (unsigned long)meta.row_size_bytes,
                 (unsigned long)LV_DRAW_BUF_SIZE(viewport_w, viewport_h,
                                                 LV_COLOR_FORMAT_NATIVE));
    bmp_release_canvas();
    bmp_release_doc();
    return false;
  }

  strncpy(s_state.path, path, sizeof(s_state.path) - 1);
  s_state.path[sizeof(s_state.path) - 1] = '\0';
  s_state.meta = meta;
  s_state.viewport_w = viewport_w;
  s_state.viewport_h = viewport_h;
  s_state.fit_scale = bmp_fit_scale(&meta, viewport_w, viewport_h);
  s_state.cached_row = -1;

  platform_log(PLATFORM_LOG_INFO, TAG, "Opening bmp preview: %s", path);
  platform_log(PLATFORM_LOG_INFO, TAG, "bmp meta %dx%d bpp=%u scale_fit=%u", meta.width,
               meta.height, (unsigned)meta.bpp, (unsigned)s_state.fit_scale);

  ui_chrome_detach(&s_chrome);
  lv_obj_clean(args->screen);
  ui_theme_apply_screen(args->screen);
  s_chrome = ui_chrome_create(args->screen, fm_base_name(path));

  s_viewport = lv_obj_create(args->screen);
  lv_obj_remove_flag(s_viewport, LV_OBJ_FLAG_SCROLLABLE);
  ui_theme_style_panel(s_viewport);
  lv_obj_set_size(s_viewport, viewport_w, viewport_h);
  lv_obj_align(s_viewport, LV_ALIGN_TOP_MID, 0, top);
  lv_obj_set_style_pad_all(s_viewport, 0, 0);
  lv_obj_set_style_radius(s_viewport, 0, 0);

  s_canvas = lv_canvas_create(s_viewport);
  lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_canvas_set_draw_buf(s_canvas, s_state.canvas_draw_buf);
  lv_obj_set_size(s_canvas, viewport_w, viewport_h);
  lv_obj_set_pos(s_canvas, 0, 0);

  s_status_label = lv_label_create(args->screen);
  ui_theme_style_label_secondary(s_status_label);
  lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_LEFT, 8, -2);

  s_active = true;
  bmp_reset_view(true);
  return true;
}

static void preview_bmp_close(void) {
  bmp_release_canvas();
  bmp_release_doc();
  ui_chrome_detach(&s_chrome);
  s_viewport = NULL;
  s_canvas = NULL;
  s_status_label = NULL;
  memset(&s_state, 0, sizeof(s_state));
  s_active = false;
}

static bool preview_bmp_on_key(const input_gamepad_state *gp, const bool edge[]) {
  (void)gp;
  if (!s_active)
    return false;

  if (edge[GAMEPAD_INPUT_MENU]) {
    ui_backlight_toggle();
    return true;
  }
  if (!ui_backlight_is_on())
    return false;

  if (edge[GAMEPAD_INPUT_L]) {
    s_state.scale = bmp_zoom_next(s_state.scale, false);
    bmp_apply_transform();
    return true;
  }
  if (edge[GAMEPAD_INPUT_R]) {
    s_state.scale = bmp_zoom_next(s_state.scale, true);
    bmp_apply_transform();
    return true;
  }
  if (edge[GAMEPAD_INPUT_A]) {
    bmp_reset_view(true);
    return true;
  }
  if (edge[GAMEPAD_INPUT_START]) {
    bmp_reset_view(false);
    return true;
  }

  int32_t step = (int32_t)LV_MAX(16, LV_MIN(s_state.viewport_w, s_state.viewport_h) / 10);
  if (edge[GAMEPAD_INPUT_LEFT]) {
    bmp_pan_by(step, 0);
    return true;
  }
  if (edge[GAMEPAD_INPUT_RIGHT]) {
    bmp_pan_by(-step, 0);
    return true;
  }
  if (edge[GAMEPAD_INPUT_UP]) {
    bmp_pan_by(0, step);
    return true;
  }
  if (edge[GAMEPAD_INPUT_DOWN]) {
    bmp_pan_by(0, -step);
    return true;
  }

  return false;
}

static void preview_bmp_on_timer(void) {
}

const preview_app_t preview_bmp_app = {
    .id = "bmp",
    .can_open = preview_bmp_can_open,
    .open = preview_bmp_open,
    .close = preview_bmp_close,
    .on_key = preview_bmp_on_key,
    .on_timer = preview_bmp_on_timer,
};
