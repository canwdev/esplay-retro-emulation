#include "preview_bmp.h"

#include "file_manager.h"
#include "hal_display.h"
#include "platform_mem.h"
#include "platform_log.h"
#include "platform_time.h"
#include "ui_backlight.h"
#include "input_repeat.h"
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
#define BMP_CANVAS_RESERVE_BYTES      (8U * 1024U)
#define BMP_FIT_CANVAS_RESERVE_BYTES  (4U * 1024U)

#define BMP_HDR_SIG           0
#define BMP_HDR_FILE_SIZE     2
#define BMP_HDR_DATA_OFFSET  10
#define BMP_HDR_DIB_SIZE     14
#define BMP_HDR_WIDTH        18
#define BMP_HDR_HEIGHT       22
#define BMP_HDR_PLANES       26
#define BMP_HDR_BPP          28
#define BMP_HDR_COMPRESSION  30
#define BMP_HDR_SIZE          54

#define BMP_SCALE_FP  16

typedef uint16_t (*bmp_decode_fn)(const uint8_t *src);

typedef struct {
  int32_t width;
  int32_t height;
  uint16_t bpp;
  uint32_t compression;
  uint32_t data_offset;
  uint32_t row_size_bytes;
  uint32_t file_size;
  bool top_down;
} bmp_meta_t;

#define BMP_CANVAS_TILES_MAX  16
#define BMP_CANVAS_TILES_MIN   8

typedef struct {
  char path[FM_PATH_MAX];
  char cwd[FM_PATH_MAX];
  bmp_meta_t meta;
  FILE *file;
  uint8_t *row_buf;
  bmp_decode_fn decode_fn;
  uint8_t pixel_bytes;
  lv_draw_buf_t *canvas_draw_bufs[BMP_CANVAS_TILES_MAX];
  uint32_t scale;
  uint32_t fit_scale;
  int32_t pan_x;
  int32_t pan_y;
  lv_coord_t viewport_w;
  lv_coord_t viewport_h;
  lv_coord_t canvas_w;
  lv_coord_t canvas_h;
  uint32_t canvas_scale;
  int32_t cached_row;
  int32_t canvas_tiles;
  bool fit_mode;
  lv_obj_t *label_1to1;
  lv_obj_t *label_status;
  char *shared_names;
  int shared_count;
  int shared_index;
  int shared_name_stride;
  lv_obj_t *screen;
} bmp_state_t;

static bmp_state_t s_state;
static lv_obj_t *s_canvases[BMP_CANVAS_TILES_MAX];
static bool s_active;

#define BMP_PAN_ACCEL_EVERY  4
#define BMP_PAN_MAX_SCALE    8

static input_repeat_state_t s_pan_repeat;
static const input_repeat_config_t s_bmp_pan_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = BMP_PAN_ACCEL_EVERY,
    .max_scale = BMP_PAN_MAX_SCALE,
};

static uint32_t bmp_zoom_next(uint32_t scale, bool zoom_in);
static void preview_bmp_close(void);
static bool bmp_open_document(const char *path);
static bool bmp_apply_path(const char *path, bool preserve_mode);
static bool bmp_is_fit_view(void);
static bool bmp_image_fits_screen(void);
static bool bmp_should_treat_as_fit(void);
static const char *preview_bmp_current_path(void);

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

static uint32_t bmp_measure_file_size(FILE *f) {
  long cur;
  long end;
  if (!f)
    return 0;
  cur = ftell(f);
  if (cur < 0)
    cur = 0;
  if (fseek(f, 0, SEEK_END) != 0)
    return 0;
  end = ftell(f);
  if (end < 0)
    end = 0;
  if (fseek(f, cur, SEEK_SET) != 0)
    return 0;
  return (uint32_t)end;
}

static bool bmp_load_meta(FILE *f, const char *path, bmp_meta_t *meta) {
  uint8_t header[BMP_HDR_SIZE];
  uint64_t need_size;
  if (fseek(f, 0, SEEK_SET) != 0)
    return false;
  size_t n = fread(header, 1, sizeof(header), f);
  if (n != sizeof(header)) {
    platform_log(PLATFORM_LOG_WARN, TAG, "short bmp header: %s", path);
    return false;
  }

  if (header[BMP_HDR_SIG] != 'B' || header[BMP_HDR_SIG + 1] != 'M') {
    platform_log(PLATFORM_LOG_WARN, TAG, "not a bmp file: %s", path);
    return false;
  }

  uint32_t dib_size = bmp_u32(&header[BMP_HDR_DIB_SIZE]);
  int32_t width = bmp_s32(&header[BMP_HDR_WIDTH]);
  int32_t height = bmp_s32(&header[BMP_HDR_HEIGHT]);
  uint16_t planes = bmp_u16(&header[BMP_HDR_PLANES]);
  uint16_t bpp = bmp_u16(&header[BMP_HDR_BPP]);
  uint32_t compression = bmp_u32(&header[BMP_HDR_COMPRESSION]);
  uint32_t data_offset = bmp_u32(&header[BMP_HDR_DATA_OFFSET]);
  bool top_down = false;

  if (height < 0) {
    height = -height;
    top_down = true;
  }

  if (dib_size < 40 || width <= 0 || height <= 0 || planes != 1 ||
      data_offset < BMP_HDR_SIZE) {
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
  meta->file_size = bmp_measure_file_size(f);
  meta->top_down = top_down;

  need_size = (uint64_t)meta->data_offset +
              (uint64_t)meta->row_size_bytes * (uint64_t)meta->height;
  if (meta->file_size > 0 && need_size > meta->file_size) {
    platform_log(PLATFORM_LOG_WARN, TAG,
                 "bmp truncated: %s size=%lu need=%llu off=%lu row=%lu h=%d",
                 path, (unsigned long)meta->file_size,
                 (unsigned long long)need_size,
                 (unsigned long)meta->data_offset,
                 (unsigned long)meta->row_size_bytes, (int)meta->height);
    return false;
  }
  return true;
}

static uint32_t bmp_fit_scale(const bmp_meta_t *meta, lv_coord_t max_w,
                              lv_coord_t max_h) {
  uint32_t sx = (uint32_t)(((uint64_t)max_w * LV_SCALE_NONE) / meta->width);
  uint32_t sy = (uint32_t)(((uint64_t)max_h * LV_SCALE_NONE) / meta->height);
  uint32_t scale = LV_MIN(sx, sy);
  if (scale == 0)
    scale = 1;
  if (scale > BMP_MAX_SCALE)
    scale = BMP_MAX_SCALE;
  return scale;
}

static int32_t bmp_scaled_dim(int32_t src, uint32_t scale) {
  return (int32_t)(((int64_t)src * (int64_t)scale + (LV_SCALE_NONE - 1)) /
                   LV_SCALE_NONE);
}

static void bmp_scaled_size(uint32_t scale, lv_coord_t *w, lv_coord_t *h) {
  lv_coord_t draw_w = (lv_coord_t)bmp_scaled_dim(s_state.meta.width, scale);
  lv_coord_t draw_h = (lv_coord_t)bmp_scaled_dim(s_state.meta.height, scale);
  if (draw_w < 1)
    draw_w = 1;
  if (draw_h < 1)
    draw_h = 1;
  if (w)
    *w = draw_w;
  if (h)
    *h = draw_h;
}

static uint32_t bmp_canvas_bytes(lv_coord_t w, lv_coord_t h) {
  return (uint32_t)LV_DRAW_BUF_SIZE(w, h, LV_COLOR_FORMAT_NATIVE);
}

static uint32_t bmp_current_canvas_bytes(void) {
  uint32_t total = 0;
  for (int i = 0; i < s_state.canvas_tiles; i++) {
    if (s_state.canvas_draw_bufs[i])
      total += s_state.canvas_draw_bufs[i]->data_size;
  }
  return total;
}



static int bmp_tile_count_for_height(lv_coord_t h) {
  if (h <= 160) return BMP_CANVAS_TILES_MIN;
  return BMP_CANVAS_TILES_MAX;
}

static bool bmp_scale_fits_budget_with_reserve(uint32_t scale,
                                               uint32_t reserve_bytes) {
  lv_coord_t draw_w;
  lv_coord_t draw_h;
  bmp_scaled_size(scale, &draw_w, &draw_h);
  
  lv_coord_t req_w = LV_MIN(draw_w, s_state.viewport_w);
  lv_coord_t req_h = LV_MIN(draw_h, s_state.viewport_h);

  uint32_t need = bmp_canvas_bytes(req_w, req_h);
  int ntiles = bmp_tile_count_for_height(req_h);
  lv_coord_t tile_h = (req_h + ntiles - 1) / ntiles;
  uint32_t tile_need = bmp_canvas_bytes(req_w, tile_h);

#ifdef TARGET_SIM
  return true;
#else
  uint32_t largest = platform_largest_free_block();
  uint32_t free_heap = platform_free_heap();
  uint32_t current = bmp_current_canvas_bytes();
  
  if (largest < tile_need)
    return false;
  if (free_heap + current < need + reserve_bytes)
    return false;
  return true;
#endif
}

static uint32_t bmp_scale_fit_budget_with_reserve(uint32_t scale,
                                                  uint32_t reserve_bytes) {
  uint32_t cur = scale;
  while (cur > BMP_MIN_SCALE &&
         !bmp_scale_fits_budget_with_reserve(cur, reserve_bytes)) {
    uint32_t next = bmp_zoom_next(cur, false);
    if (next >= cur)
      break;
    cur = next;
  }
  if (!bmp_scale_fits_budget_with_reserve(cur, reserve_bytes))
    return 0;
  return cur;
}

static uint16_t bmp_decode_16(const uint8_t *src) {
  uint16_t v = bmp_u16(src);
  uint8_t r = (uint8_t)(((v >> 11) & 0x1F) << 3);
  uint8_t g = (uint8_t)(((v >> 5) & 0x3F) << 2);
  uint8_t b = (uint8_t)((v & 0x1F) << 3);
  return lv_color_to_u16(lv_color_make(r, g, b));
}

static uint16_t bmp_decode_24(const uint8_t *src) {
  return lv_color_to_u16(lv_color_make(src[2], src[1], src[0]));
}

static void bmp_release_doc(void) {
  if (s_state.file) {
    fclose(s_state.file);
    s_state.file = NULL;
  }
  platform_free(s_state.row_buf);
  s_state.row_buf = NULL;
}

static void bmp_release_shared_list(void) {
  platform_free(s_state.shared_names);
  s_state.shared_names = NULL;
  s_state.shared_count = 0;
  s_state.shared_index = -1;
  s_state.shared_name_stride = 0;
}

static void bmp_release_canvas(void) {
  for (int i = 0; i < s_state.canvas_tiles; i++) {
    if (s_state.canvas_draw_bufs[i]) {
      lv_draw_buf_destroy(s_state.canvas_draw_bufs[i]);
      s_state.canvas_draw_bufs[i] = NULL;
    }
    if (s_canvases[i]) {
      lv_obj_delete(s_canvases[i]);
      s_canvases[i] = NULL;
    }
  }
  s_state.canvas_w = 0;
  s_state.canvas_h = 0;
  s_state.canvas_scale = 0;
}

static bool bmp_load_row(int32_t src_y) {
  long pos;
  int32_t file_row;

  if (!s_state.file || !s_state.row_buf) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "row load precheck failed file=%p row_buf=%p",
                 (void *)s_state.file, (void *)s_state.row_buf);
    return false;
  }
  if (src_y < 0 || src_y >= s_state.meta.height) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "row out of range y=%d h=%d", (int)src_y,
                 (int)s_state.meta.height);
    return false;
  }
  if (s_state.cached_row == src_y)
    return true;

  file_row = s_state.meta.top_down ? src_y : (s_state.meta.height - 1 - src_y);
  pos = (long)(s_state.meta.data_offset +
               s_state.meta.row_size_bytes * (uint32_t)file_row);
  if (fseek(s_state.file, pos, SEEK_SET) != 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG,
                 "row seek failed y=%d file_row=%d pos=%ld off=%lu row=%lu file=%lu",
                 (int)src_y, (int)file_row, pos,
                 (unsigned long)s_state.meta.data_offset,
                 (unsigned long)s_state.meta.row_size_bytes,
                 (unsigned long)s_state.meta.file_size);
    return false;
  }
  clearerr(s_state.file);
  size_t n = fread(s_state.row_buf, 1, s_state.meta.row_size_bytes, s_state.file);
  if (n != s_state.meta.row_size_bytes) {
    platform_log(PLATFORM_LOG_ERROR, TAG,
                 "row read failed y=%d file_row=%d pos=%ld got=%lu need=%lu eof=%d err=%d",
                 (int)src_y, (int)file_row, pos, (unsigned long)n,
                 (unsigned long)s_state.meta.row_size_bytes, feof(s_state.file),
                 ferror(s_state.file));
    return false;
  }

  s_state.cached_row = src_y;
  return true;
}

static void bmp_clamp_pan(int32_t draw_w, int32_t draw_h) {
  int32_t max_pan_x = 0;
  int32_t min_pan_x = 0;
  int32_t max_pan_y = 0;
  int32_t min_pan_y = 0;
  if (draw_w > s_state.viewport_w) {
    max_pan_x = (draw_w - s_state.viewport_w) / 2;
    min_pan_x = (int32_t)s_state.canvas_w - draw_w + max_pan_x;
  }
  if (draw_h > s_state.viewport_h) {
    max_pan_y = (draw_h - s_state.viewport_h) / 2;
    min_pan_y = (int32_t)s_state.canvas_h - draw_h + max_pan_y;
  }

  if (s_state.pan_x > max_pan_x)
    s_state.pan_x = max_pan_x;
  if (s_state.pan_x < min_pan_x)
    s_state.pan_x = min_pan_x;
  if (s_state.pan_y > max_pan_y)
    s_state.pan_y = max_pan_y;
  if (s_state.pan_y < min_pan_y)
    s_state.pan_y = min_pan_y;
}

static bool bmp_ensure_canvas(uint32_t scale) {
  lv_coord_t draw_w;
  lv_coord_t draw_h;
  uint32_t need;
  uint32_t current;

  bmp_scaled_size(scale, &draw_w, &draw_h);
  lv_coord_t req_w = LV_MIN(draw_w, s_state.viewport_w);
  lv_coord_t req_h = LV_MIN(draw_h, s_state.viewport_h);

#ifndef TARGET_SIM
  current = bmp_current_canvas_bytes();
  uint32_t free_heap = platform_free_heap();
  uint32_t max_bytes = free_heap + current;
  if (max_bytes > BMP_CANVAS_RESERVE_BYTES) {
    max_bytes -= BMP_CANVAS_RESERVE_BYTES;
  } else {
    max_bytes = 0;
  }
  
  while (req_w > 16 && req_h > 16 && bmp_canvas_bytes(req_w, req_h) > max_bytes) {
    req_w = (req_w * 9) / 10;
    req_h = (req_h * 9) / 10;
  }

  platform_log(PLATFORM_LOG_INFO, TAG,
               "canvas budget scale=%u req=%dx%d max_bytes=%lu free=%lu current=%lu",
               (unsigned)scale, (int)req_w, (int)req_h,
               (unsigned long)max_bytes, (unsigned long)free_heap,
               (unsigned long)current);
#endif

  int ntiles_new = bmp_tile_count_for_height(req_h);

  if (s_state.canvas_draw_bufs[0] && s_state.canvas_w == req_w &&
      s_state.canvas_h == req_h && s_state.canvas_tiles == ntiles_new) {
    s_state.canvas_scale = scale;
    return true;
  }

  need = bmp_canvas_bytes(req_w, req_h);
  current = bmp_current_canvas_bytes();

  if (s_state.canvas_draw_bufs[0] && need <= current) {
    bmp_release_canvas();
  }

  bmp_release_canvas();
  
  s_state.canvas_tiles = ntiles_new;
  lv_coord_t tile_h = (req_h + ntiles_new - 1) / ntiles_new;
  
  for (int i = 0; i < ntiles_new; i++) {
    lv_coord_t th = tile_h;
    if (i * tile_h >= req_h) {
      th = 0;
    } else if ((i + 1) * tile_h > req_h) {
      th = req_h - i * tile_h;
    }
    
    if (th > 0) {
      lv_draw_buf_t *draw_buf = lv_draw_buf_create((uint32_t)req_w, (uint32_t)th,
                                                    LV_COLOR_FORMAT_NATIVE, LV_STRIDE_AUTO);
      if (!draw_buf) {
        platform_log(PLATFORM_LOG_WARN, TAG,
                     "alloc failed for tile %d row=%lu th=%d largest=%lu free=%lu scale=%u",
                     i, (unsigned long)s_state.meta.row_size_bytes, (int)th,
                     (unsigned long)platform_largest_free_block(),
                     (unsigned long)platform_free_heap(), (unsigned)scale);
        bmp_release_canvas();
        return false;
      }
      s_state.canvas_draw_bufs[i] = draw_buf;
      if (!s_canvases[i] && s_state.screen) {
        s_canvases[i] = lv_canvas_create(s_state.screen);
        lv_obj_remove_flag(s_canvases[i], LV_OBJ_FLAG_SCROLLABLE);
      }
      if (s_canvases[i]) {
        lv_canvas_set_draw_buf(s_canvases[i], draw_buf);
        lv_obj_set_size(s_canvases[i], req_w, th);
        lv_obj_remove_flag(s_canvases[i], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      if (s_canvases[i]) {
        lv_obj_delete(s_canvases[i]);
        s_canvases[i] = NULL;
      }
    }
  }

  s_state.canvas_w = req_w;
  s_state.canvas_h = req_h;
  s_state.canvas_scale = scale;
  return true;
}

static bool bmp_render_canvas(void) {
  lv_coord_t draw_w, draw_h;
  bmp_scaled_size(s_state.canvas_scale, &draw_w, &draw_h);

  int32_t view_off_x = 0;
  if (draw_w > s_state.viewport_w) {
    view_off_x = (draw_w - s_state.viewport_w) / 2 - s_state.pan_x;
  }

  int32_t view_off_y = 0;
  if (draw_h > s_state.viewport_h) {
    view_off_y = (draw_h - s_state.viewport_h) / 2 - s_state.pan_y;
  }

  s_state.cached_row = -1;

  int ntiles = s_state.canvas_tiles;
  lv_coord_t tile_h = (s_state.canvas_h + ntiles - 1) / ntiles;
  bool is_1to1 = (s_state.canvas_scale == LV_SCALE_NONE);
  uint8_t px_bytes = s_state.pixel_bytes;
  bmp_decode_fn decode = s_state.decode_fn;

  if (is_1to1) {
    int32_t src_start_x = view_off_x;
    for (int t = 0; t < ntiles; t++) {
      lv_draw_buf_t *draw_buf = s_state.canvas_draw_bufs[t];
      if (!draw_buf) continue;
      lv_draw_buf_clear(draw_buf, NULL);
      lv_coord_t th = draw_buf->header.h;

      for (int32_t y = 0; y < th; ++y) {
        int32_t global_y = t * tile_h + y;
        int32_t src_y = global_y + view_off_y;
        if (src_y < 0) src_y = 0;
        if (src_y >= s_state.meta.height) src_y = s_state.meta.height - 1;
        if (!bmp_load_row(src_y)) return false;

        uint16_t *dst = (uint16_t *)((uint8_t *)draw_buf->data +
                                      (size_t)y * draw_buf->header.stride);
        int32_t src_x = src_start_x;
        for (int32_t x = 0; x < s_state.canvas_w; ++x) {
          int32_t sx = src_x;
          if (sx < 0) sx = 0;
          else if (sx >= s_state.meta.width) sx = s_state.meta.width - 1;
          dst[x] = decode(s_state.row_buf + (size_t)sx * px_bytes);
          src_x++;
        }
      }
      lv_draw_buf_flush_cache(draw_buf, NULL);
      if (s_canvases[t]) lv_obj_invalidate(s_canvases[t]);
      platform_sleep_ms(1);
    }
  } else {
    uint32_t step_x = ((uint64_t)s_state.meta.width << BMP_SCALE_FP) / (uint64_t)draw_w;
    uint32_t acc_x0 = (uint32_t)((int64_t)view_off_x * (int64_t)step_x);
    for (int t = 0; t < ntiles; t++) {
      lv_draw_buf_t *draw_buf = s_state.canvas_draw_bufs[t];
      if (!draw_buf) continue;
      lv_draw_buf_clear(draw_buf, NULL);
      lv_coord_t th = draw_buf->header.h;

      for (int32_t y = 0; y < th; ++y) {
        int32_t global_y = t * tile_h + y;
        int32_t scaled_y = global_y + view_off_y;
        int32_t src_y = ((int64_t)scaled_y * s_state.meta.height) / draw_h;
        if (src_y < 0) src_y = 0;
        if (src_y >= s_state.meta.height) src_y = s_state.meta.height - 1;
        if (!bmp_load_row(src_y)) return false;

        uint16_t *dst = (uint16_t *)((uint8_t *)draw_buf->data +
                                      (size_t)y * draw_buf->header.stride);
        int32_t max_src_x = s_state.meta.width - 1;
        uint32_t acc_x = acc_x0;
        for (int32_t x = 0; x < s_state.canvas_w; ++x) {
          int32_t sx = (int32_t)(acc_x >> BMP_SCALE_FP);
          acc_x += step_x;
          if (sx < 0) sx = 0;
          else if (sx > max_src_x) sx = max_src_x;
          dst[x] = decode(s_state.row_buf + (size_t)sx * px_bytes);
        }
      }
      lv_draw_buf_flush_cache(draw_buf, NULL);
      if (s_canvases[t]) lv_obj_invalidate(s_canvases[t]);
      platform_sleep_ms(1);
    }
  }
  return true;
}

static bool bmp_apply_transform(void) {
  lv_coord_t draw_w;
  lv_coord_t draw_h;

  bmp_scaled_size(s_state.scale, &draw_w, &draw_h);

  if (!bmp_ensure_canvas(s_state.scale))
    return false;

  bmp_clamp_pan(draw_w, draw_h);

  if (s_state.canvas_scale != s_state.scale) {
    platform_log(PLATFORM_LOG_ERROR, TAG,
                 "canvas scale mismatch want=%u got=%u",
                 (unsigned)s_state.scale, (unsigned)s_state.canvas_scale);
    return false;
  }
  
  if (!bmp_render_canvas()) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "render failed: %s", s_state.path);
    return false;
  }
  
  int ntiles = s_state.canvas_tiles;
  lv_coord_t tile_h = (s_state.canvas_h + ntiles - 1) / ntiles;
  for (int i = 0; i < ntiles; i++) {
    if (s_canvases[i] && s_state.canvas_draw_bufs[i]) {
      lv_obj_set_pos(s_canvases[i], (s_state.viewport_w - s_state.canvas_w) / 2,
                     (s_state.viewport_h - s_state.canvas_h) / 2 + i * tile_h);
    }
  }

  if (s_state.label_1to1) {
    if (!bmp_should_treat_as_fit())
      lv_obj_remove_flag(s_state.label_1to1, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_state.label_1to1, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_state.label_status) {
    if (bmp_should_treat_as_fit())
      lv_obj_remove_flag(s_state.label_status, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_state.label_status, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_state.label_1to1)
    lv_obj_move_foreground(s_state.label_1to1);
  if (s_state.label_status)
    lv_obj_move_foreground(s_state.label_status);

  return true;
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

static bool bmp_set_scale_with_reserve(uint32_t scale, bool allow_reduce,
                                       uint32_t reserve_bytes) {
  uint32_t target = scale;
  if (allow_reduce) {
    target = bmp_scale_fit_budget_with_reserve(target, reserve_bytes);
    if (target == 0)
      return false;
  }
  s_state.scale = target;
  s_state.pan_x = 0;
  s_state.pan_y = 0;
  return bmp_apply_transform();
}

static bool bmp_set_scale(uint32_t scale, bool allow_reduce) {
  return bmp_set_scale_with_reserve(scale, allow_reduce,
                                    BMP_CANVAS_RESERVE_BYTES);
}

static bool bmp_reset_view(bool fit_to_screen) {
  if (fit_to_screen) {
    if (!bmp_set_scale_with_reserve(s_state.fit_scale, true,
                                    BMP_FIT_CANVAS_RESERVE_BYTES))
      return false;
  } else {
    if (!bmp_set_scale(LV_SCALE_NONE, false))
      return false;
  }
  s_state.fit_mode = fit_to_screen;
  return true;
}

static bool bmp_is_fit_view(void) {
  return s_state.fit_mode;
}

static bool bmp_image_fits_screen(void) {
  return s_state.meta.width <= s_state.viewport_w &&
         s_state.meta.height <= s_state.viewport_h;
}

static bool bmp_should_treat_as_fit(void) {
  return bmp_is_fit_view() || bmp_image_fits_screen();
}

static void bmp_update_status_label(void) {
  if (!s_state.label_status)
    return;

  const char *filename = strrchr(s_state.path, '/');
  if (filename)
    filename++;
  else
    filename = s_state.path;

  if (s_state.shared_count > 1)
    lv_label_set_text_fmt(s_state.label_status, "[%d/%d] %s",
                          s_state.shared_index + 1, s_state.shared_count,
                          filename);
  else
    lv_label_set_text_fmt(s_state.label_status, "%s", filename);
}

static bool preview_bmp_can_open(const char *path) {
  const char *dot = strrchr(path, '.');
  return dot && strcasecmp(dot, ".bmp") == 0;
}

static bool bmp_open_document(const char *path) {
  FILE *file;
  bmp_meta_t meta;
  uint8_t *row_buf;
  uint32_t fit_scale;

  file = bmp_fopen_native(path, "rb");
  if (!file)
    return false;
  if (!bmp_load_meta(file, path, &meta)) {
    fclose(file);
    return false;
  }

  row_buf = (uint8_t *)platform_malloc(meta.row_size_bytes);
  if (!row_buf) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "alloc failed row=%lu",
                 (unsigned long)meta.row_size_bytes);
    fclose(file);
    return false;
  }

  fit_scale = bmp_scale_fit_budget_with_reserve(
      bmp_fit_scale(&meta, s_state.viewport_w, s_state.viewport_h),
      BMP_FIT_CANVAS_RESERVE_BYTES);
  if (fit_scale == 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG,
                 "no bmp scale fits memory w=%d h=%d largest=%lu free=%lu",
                 meta.width, meta.height,
                 (unsigned long)platform_largest_free_block(),
                 (unsigned long)platform_free_heap());
    platform_free(row_buf);
    fclose(file);
    return false;
  }

  bmp_release_canvas();
  bmp_release_doc();

  s_state.file = file;
  s_state.row_buf = row_buf;
  s_state.meta = meta;
  s_state.decode_fn = (meta.bpp == 16) ? bmp_decode_16 : bmp_decode_24;
  s_state.pixel_bytes = (uint8_t)(meta.bpp / 8);
  s_state.canvas_tiles = bmp_tile_count_for_height(LV_MIN(
      bmp_scaled_dim(meta.height, fit_scale), s_state.viewport_h));
  s_state.fit_scale = fit_scale;
  s_state.cached_row = -1;
  strncpy(s_state.path, path, sizeof(s_state.path) - 1);
  s_state.path[sizeof(s_state.path) - 1] = '\0';

  platform_log(PLATFORM_LOG_INFO, TAG, "Opening bmp preview: %s", path);
  platform_log(PLATFORM_LOG_INFO, TAG,
               "bmp meta %dx%d bpp=%u scale_fit=%u off=%lu row=%lu size=%lu",
               meta.width, meta.height, (unsigned)meta.bpp,
               (unsigned)s_state.fit_scale, (unsigned long)meta.data_offset,
               (unsigned long)meta.row_size_bytes, (unsigned long)meta.file_size);
  return true;
}

static bool bmp_apply_path(const char *path, bool preserve_mode) {
  bool was_fit = bmp_is_fit_view();
  if (!bmp_open_document(path))
    return false;

  bmp_update_status_label();

  if (preserve_mode && !was_fit) {
    if (!bmp_reset_view(false)) {
      platform_log(PLATFORM_LOG_WARN, TAG, "1:1 rejected by memory");
      return bmp_reset_view(true);
    }
    return true;
  }

  return bmp_reset_view(true);
}

static const char *bmp_shared_name_at(int index) {
  if (!s_state.shared_names || s_state.shared_count <= 0 ||
      s_state.shared_name_stride <= 0)
    return NULL;
  if (index < 0 || index >= s_state.shared_count)
    return NULL;
  return s_state.shared_names + (size_t)index * (size_t)s_state.shared_name_stride;
}

static bool bmp_switch_relative(int delta) {
  char full[FM_PATH_MAX];
  const char *name;
  int next_index;

  if (s_state.shared_count <= 1)
    return true;

  next_index = s_state.shared_index + delta;
  if (next_index < 0)
    next_index = s_state.shared_count - 1;
  else if (next_index >= s_state.shared_count)
    next_index = 0;

  name = bmp_shared_name_at(next_index);
  if (!name || !name[0])
    return false;

  if (snprintf(full, sizeof(full), "%s/%s", s_state.cwd, name) < 0 ||
      strlen(full) >= sizeof(full))
    return false;
  if (!bmp_apply_path(full, true))
    return false;

  s_state.shared_index = next_index;
  return true;
}

static bool preview_bmp_open(const char *path, preview_open_args_t *args) {
  lv_coord_t viewport_w = HAL_DISPLAY_WIDTH;
  lv_coord_t viewport_h = HAL_DISPLAY_HEIGHT;

  input_repeat_reset(&s_pan_repeat);
  memset(&s_state, 0, sizeof(s_state));
  s_state.viewport_w = viewport_w;
  s_state.viewport_h = viewport_h;
  s_state.canvas_tiles = bmp_tile_count_for_height(viewport_h);
  s_state.screen = args->screen;
  if (args->cwd)
    strlcpy(s_state.cwd, args->cwd, sizeof(s_state.cwd));
  s_state.shared_name_stride = args->shared_name_stride;
  s_state.shared_count = args->shared_count;
  s_state.shared_index = args->shared_index;
  if (args->shared_names && args->shared_count > 0 && args->shared_name_stride > 0) {
    s_state.shared_names = (char *)args->shared_names;
    args->shared_names = NULL;
  }

  lv_obj_clean(args->screen);
  lv_obj_set_style_pad_all(args->screen, 0, 0);
  lv_obj_set_style_border_width(args->screen, 0, 0);

  s_state.label_1to1 = lv_label_create(args->screen);
  lv_label_set_text(s_state.label_1to1, "1:1");
  ui_theme_style_label_accent(s_state.label_1to1);
  lv_obj_align(s_state.label_1to1, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
  lv_obj_add_flag(s_state.label_1to1, LV_OBJ_FLAG_HIDDEN);

  s_state.label_status = lv_label_create(args->screen);
  ui_theme_style_label_secondary(s_state.label_status);
  ui_theme_style_label_truncated(s_state.label_status, s_state.viewport_w - 16);
  lv_obj_align(s_state.label_status, LV_ALIGN_BOTTOM_MID, 0, -4);

  s_active = true;
  if (!bmp_apply_path(path, false)) {
    preview_bmp_close();
    return false;
  }
  return true;
}

static void preview_bmp_close(void) {
  input_repeat_reset(&s_pan_repeat);
  bmp_release_canvas();
  bmp_release_doc();
  bmp_release_shared_list();
  if (s_state.label_1to1) {
    lv_obj_delete(s_state.label_1to1);
    s_state.label_1to1 = NULL;
  }
  if (s_state.label_status) {
    lv_obj_delete(s_state.label_status);
    s_state.label_status = NULL;
  }
  memset(&s_state, 0, sizeof(s_state));
  s_active = false;
}

static void bmp_pan_hold_tick(const input_gamepad_state *gp) {
  if (bmp_should_treat_as_fit())
    return;

  bool left  = gp->values[GAMEPAD_INPUT_LEFT] == 1;
  bool right = gp->values[GAMEPAD_INPUT_RIGHT] == 1;
  bool up    = gp->values[GAMEPAD_INPUT_UP] == 1;
  bool down  = gp->values[GAMEPAD_INPUT_DOWN] == 1;

  int held_count = (left ? 1 : 0) + (right ? 1 : 0) +
                   (up ? 1 : 0) + (down ? 1 : 0);
  if (held_count != 1) {
    input_repeat_reset(&s_pan_repeat);
    return;
  }

  uint32_t dir = 0;
  if (left)  dir = GAMEPAD_INPUT_LEFT;
  if (right) dir = GAMEPAD_INPUT_RIGHT;
  if (up)    dir = GAMEPAD_INPUT_UP;
  if (down)  dir = GAMEPAD_INPUT_DOWN;

  uint32_t now = lv_tick_get();
  uint16_t repeat_count = 0;
  if (!input_repeat_tick(&s_pan_repeat, true, dir, now,
                         &s_bmp_pan_repeat, &repeat_count))
    return;

  int scale = input_repeat_scale_for_count(&s_bmp_pan_repeat, repeat_count);
  int32_t step = (int32_t)LV_MAX(16, LV_MIN(s_state.viewport_w,
                                             s_state.viewport_h) / 10);
  if (dir == GAMEPAD_INPUT_LEFT)
    bmp_pan_by(step * scale, 0);
  else if (dir == GAMEPAD_INPUT_RIGHT)
    bmp_pan_by(-step * scale, 0);
  else if (dir == GAMEPAD_INPUT_UP)
    bmp_pan_by(0, step * scale);
  else if (dir == GAMEPAD_INPUT_DOWN)
    bmp_pan_by(0, -step * scale);
}

static bool preview_bmp_on_key(const input_gamepad_state *gp, const bool edge[]) {
  (void)gp;
  if (!s_active)
    return false;

  if (edge[GAMEPAD_INPUT_MENU]) {
    input_repeat_reset(&s_pan_repeat);
    ui_backlight_toggle();
    return true;
  }
  if (!ui_backlight_is_on()) {
    input_repeat_reset(&s_pan_repeat);
    return false;
  }

  if (edge[GAMEPAD_INPUT_L]) {
    input_repeat_reset(&s_pan_repeat);
    bmp_switch_relative(-1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_R]) {
    input_repeat_reset(&s_pan_repeat);
    bmp_switch_relative(1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_A]) {
    input_repeat_reset(&s_pan_repeat);
    if (bmp_image_fits_screen())
      return true;
    if (bmp_is_fit_view()) {
      if (!bmp_reset_view(false))
        platform_log(PLATFORM_LOG_WARN, TAG, "1:1 rejected by memory");
    } else {
      if (!bmp_reset_view(true))
        platform_log(PLATFORM_LOG_WARN, TAG, "fit view failed");
    }
    return true;
  }
  if (edge[GAMEPAD_INPUT_START]) {
    input_repeat_reset(&s_pan_repeat);
    return true;
  }

  int32_t step = (int32_t)LV_MAX(16, LV_MIN(s_state.viewport_w, s_state.viewport_h) / 10);

  if (bmp_should_treat_as_fit()) {
    if (edge[GAMEPAD_INPUT_LEFT]) {
      bmp_switch_relative(-1);
      return true;
    }
    if (edge[GAMEPAD_INPUT_RIGHT]) {
      bmp_switch_relative(1);
      return true;
    }
  }

  if (edge[GAMEPAD_INPUT_LEFT]) {
    bmp_pan_by(step, 0);
    input_repeat_arm(&s_pan_repeat, GAMEPAD_INPUT_LEFT, lv_tick_get(),
                     &s_bmp_pan_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_RIGHT]) {
    bmp_pan_by(-step, 0);
    input_repeat_arm(&s_pan_repeat, GAMEPAD_INPUT_RIGHT, lv_tick_get(),
                     &s_bmp_pan_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_UP]) {
    bmp_pan_by(0, step);
    input_repeat_arm(&s_pan_repeat, GAMEPAD_INPUT_UP, lv_tick_get(),
                     &s_bmp_pan_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_DOWN]) {
    bmp_pan_by(0, -step);
    input_repeat_arm(&s_pan_repeat, GAMEPAD_INPUT_DOWN, lv_tick_get(),
                     &s_bmp_pan_repeat);
    return true;
  }

  bmp_pan_hold_tick(gp);
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
    .current_path = preview_bmp_current_path,
};

static const char *preview_bmp_current_path(void) {
  return s_active ? s_state.path : NULL;
}
