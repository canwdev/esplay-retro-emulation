#include "preview_text.h"
#include "file_manager.h"
#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "settings.h"
#include "misc/lv_text_private.h"
#include "lcd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "../text/gb2312_uni.inc"

static const char *TAG = "preview_text";

#define TEXT_RAW_MAX          (256 * 1024)
#define TEXT_PAGE_MAX         4096
#define TEXT_LINE_SPACE       4
#define TEXT_PAGE_HOLD_MS_INITIAL 400
#define TEXT_PAGE_HOLD_MS_REPEAT  80

typedef struct {
  char *utf8;
  size_t utf8_len;
  uint32_t *page_off;
  int page_count;
  int cur_page;
} text_doc_t;

static text_doc_t s_doc;
static ui_chrome_t s_chrome;
static lv_obj_t *s_body_label;
static lv_obj_t *s_status_label;
static bool s_active;
static bool s_backlight_off;
static uint8_t s_backlight_restore;
static bool s_page_hold_armed;
static int8_t s_page_hold_dir;
static uint32_t s_page_hold_next_ms;

static lv_coord_t s_page_w;
static lv_coord_t s_page_h;

/* ------------------------------------------------------------------ codec */

static bool is_utf8(const uint8_t *s, size_t len) {
  for (size_t i = 0; i < len; ) {
    if (s[i] < 0x80) { i++; continue; }
    if ((s[i] & 0xE0) == 0xC0 && i + 1 < len && (s[i + 1] & 0xC0) == 0x80) { i += 2; continue; }
    if ((s[i] & 0xF0) == 0xE0 && i + 2 < len && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) { i += 3; continue; }
    if ((s[i] & 0xF8) == 0xF0 && i + 3 < len && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) { i += 4; continue; }
    return false;
  }
  return true;
}

static size_t utf8_encode(uint32_t cp, char *out) {
  if (cp <= 0x7F) { out[0] = (char)cp; return 1; }
  if (cp <= 0x7FF) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp <= 0xFFFF) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
  out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

static char *decode_text(uint8_t *raw, size_t len, size_t *out_len) {
  if (len >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { 
    memmove(raw, raw + 3, len - 3);
    len -= 3; 
  }

  if (is_utf8(raw, len)) {
    ESP_LOGI(TAG, "Decoding as UTF-8 (in-place), len: %zu", len);
    for (size_t i = 0; i < len; i++) if (raw[i] == '\r') raw[i] = '\n';
    raw[len] = '\0';
    *out_len = len;
    return (char *)raw;
  }

  /* GB2312 fallback */
  ESP_LOGI(TAG, "Decoding as GB2312, len: %zu", len);
  
  /* 1. Calculate exact required size to save RAM. 
   * GB2312 (2 bytes) -> UTF-8 (3 bytes) is 1.5x max. 
   */
  size_t required = 0;
  for (size_t i = 0; i < len; ) {
    if (raw[i] < 0x80) { required++; i++; }
    else { required += 3; i += 2; }
  }

  char *out = malloc(required + 1);
  if (!out) { 
    ESP_LOGE(TAG, "Failed to malloc for GB2312 decode (needed %zu)", required); 
    return NULL; 
  }

  size_t o = 0, i = 0;
  while (i < len) {
    if (raw[i] < 0x80) { out[o++] = (raw[i] == '\r' ? '\n' : (char)raw[i]); i++; }
    else if (i + 1 < len && raw[i] >= 0xA1 && raw[i] <= 0xF7 && raw[i+1] >= 0xA1 && raw[i+1] <= 0xFE) {
      uint16_t u = gb2312_uni[(raw[i] - 0xA1) * 94 + (raw[i+1] - 0xA1)];
      o += utf8_encode(u == 0xFFFF ? 0xFFFD : u, &out[o]);
      i += 2;
    } else { out[o++] = '?'; i++; }
  }
  out[o] = '\0'; *out_len = o;
  ESP_LOGI(TAG, "GB2312 decoded, new len: %zu", o);
  return out;
}

/* ------------------------------------------------------------------ doc */

static void text_doc_free(text_doc_t *doc) {
  free(doc->utf8);
  free(doc->page_off);
  memset(doc, 0, sizeof(*doc));
}

static int text_build_pages(text_doc_t *doc, lv_coord_t max_w, lv_coord_t max_h) {
  const lv_font_t *font = ui_font_default();
  lv_coord_t line_h = lv_font_get_line_height(font) + TEXT_LINE_SPACE;
  int max_lines = max_h / (line_h > 0 ? line_h : 14);
  if (max_lines < 1) max_lines = 1;
  ESP_LOGI(TAG, "Building pages: w=%d, h=%d, max_lines=%d", max_w, max_h, max_lines);

  lv_text_attributes_t attr;
  lv_text_attributes_init(&attr);
  attr.max_width = max_w;
  attr.line_space = TEXT_LINE_SPACE;

  int cap = 64, pages = 0;
  uint32_t *off = malloc(cap * sizeof(uint32_t));
  if (!off) { ESP_LOGE(TAG, "Failed to malloc for page offsets"); return -1; }

  size_t pos = 0;
  while (pos <= doc->utf8_len) {
    if (pages >= cap) {
      cap *= 2;
      uint32_t *n = realloc(off, cap * sizeof(uint32_t));
      if (!n) { ESP_LOGE(TAG, "Failed to realloc for page offsets"); free(off); return -1; }
      off = n;
    }
    off[pages++] = (uint32_t)pos;
    if (pos >= doc->utf8_len) break;
    for (int l = 0; l < max_lines && pos < doc->utf8_len; l++) {
      if (doc->utf8[pos] == '\n') { pos++; continue; }
      uint32_t adv = lv_text_get_next_line(&doc->utf8[pos], doc->utf8_len - pos, font, NULL, &attr);
      pos += (adv > 0 ? adv : 1);
    }
  }
  doc->page_off = off; doc->page_count = pages; doc->cur_page = 0;
  ESP_LOGI(TAG, "Total pages: %d", pages);
  return 0;
}

static void text_show_page(void) {
  if (!s_body_label || !s_doc.utf8) return;
  if (s_doc.cur_page < 0) s_doc.cur_page = 0;
  if (s_doc.cur_page >= s_doc.page_count) s_doc.cur_page = s_doc.page_count - 1;

  uint32_t start = s_doc.page_off[s_doc.cur_page];
  uint32_t end = (s_doc.cur_page + 1 < s_doc.page_count) ? s_doc.page_off[s_doc.cur_page + 1] : (uint32_t)s_doc.utf8_len;

  static char buf[TEXT_PAGE_MAX];
  size_t n = (end - start < sizeof(buf)) ? (end - start) : (sizeof(buf) - 1);
  memcpy(buf, s_doc.utf8 + start, n); buf[n] = '\0';
  lv_label_set_text(s_body_label, buf);

  if (s_status_label) {
    int pct = (s_doc.page_count > 1) ? (s_doc.cur_page * 100 / (s_doc.page_count - 1)) : 0;
    lv_label_set_text_fmt(s_status_label, "%d/%d  %d%%", s_doc.cur_page + 1, s_doc.page_count, pct);
  }
}

static bool preview_text_can_open(const char *path) {
  const char *base = fm_base_name(path);
  if (fm_is_playable_audio_filename(base)) return false;
  static const char *exts[] = { ".txt", ".text", ".md", ".json", ".xml", ".yaml", ".yml", ".ini", ".cfg", ".log", ".c", ".h", ".cpp", ".hpp", ".py", ".js", ".ts", ".sh", ".cmake", ".csv", ".html", ".htm", ".css", ".lrc", ".nfo" };
  for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
    const char *dot = strrchr(path, '.');
    if (dot && strcasecmp(dot, exts[i]) == 0) return true;
  }
  struct stat st;
  return (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= TEXT_RAW_MAX);
}

static bool preview_text_load(const char *path, text_doc_t *doc, lv_coord_t w, lv_coord_t h) {
  struct stat st;
  if (stat(path, &st) != 0 || st.st_size == 0) {
    ESP_LOGE(TAG, "stat failed or file empty: %s", path);
    return false;
  }

  size_t full_sz = (size_t)st.st_size;
  uint32_t free_heap = (uint32_t)esp_get_free_heap_size();
  
  /* 
   * Strategy:
   * 1. UTF-8 is in-place, so it needs ~ sz bytes.
   * 2. GB2312 needs sz (raw) + sz * 1.5 (decoded) = 2.5 * sz bytes.
   * We don't know the encoding yet, so we read a small header first.
   */
  FILE *f = fopen(path, "rb");
  if (!f) { ESP_LOGE(TAG, "fopen failed: %s", path); return false; }

  uint8_t header[1024];
  size_t header_len = fread(header, 1, sizeof(header), f);
  bool maybe_utf8 = is_utf8(header, header_len);
  fseek(f, 0, SEEK_SET);

  size_t sz_limit = maybe_utf8 ? (free_heap - 24576) : (free_heap / 4);
  if (sz_limit > TEXT_RAW_MAX) sz_limit = TEXT_RAW_MAX;

  size_t sz = (full_sz > sz_limit) ? sz_limit : full_sz;
  
  ESP_LOGI(TAG, "Loading file: %s, size: %zu, maybe_utf8: %d, heap free: %u, target sz: %zu", 
           path, full_sz, maybe_utf8, free_heap, sz);

retry_load:
  uint8_t *raw = NULL;
  while (sz >= 1024) {
    raw = malloc(sz + 1);
    if (raw) break;
    sz -= 4096;
  }

  if (!raw) {
    ESP_LOGE(TAG, "Failed to malloc raw buffer, heap free: %u", (unsigned)esp_get_free_heap_size());
    fclose(f);
    return false;
  }

  if (sz < full_sz) {
    ESP_LOGW(TAG, "File too large for heap, loading only first %zu bytes", sz);
  }

  fseek(f, 0, SEEK_SET);
  size_t n = fread(raw, 1, sz, f);
  fclose(f);

  memset(doc, 0, sizeof(*doc));
  char *decoded = decode_text(raw, n, &doc->utf8_len);
  if (!decoded) {
    /* If decode_text failed due to OOM, retry with a smaller chunk. */
    free(raw);
    if (sz > 4096) {
      sz /= 2;
      ESP_LOGW(TAG, "Decode failed (likely OOM), retrying with sz=%zu", sz);
      f = fopen(path, "rb");
      if (f) goto retry_load;
    }
    ESP_LOGE(TAG, "decode_text failed");
    return false;
  }

  doc->utf8 = decoded;
  if (decoded != (char *)raw) {
    free(raw);
  }
  
  if (text_build_pages(doc, w, h) != 0) {
    ESP_LOGE(TAG, "text_build_pages failed");
    text_doc_free(doc);
    return false;
  }
  return true;
}

static void preview_text_page_hold_reset(void) { s_page_hold_armed = false; s_page_hold_dir = 0; }
static void text_change_page(int delta) {
  if (s_doc.page_count <= 0) return;
  int next = s_doc.cur_page + delta;
  if (next < 0) next = 0;
  if (next >= s_doc.page_count) next = s_doc.page_count - 1;
  if (next != s_doc.cur_page) { s_doc.cur_page = next; text_show_page(); }
}

static void preview_text_page_hold_tick(const input_gamepad_state *gp) {
  bool prev = gp->values[GAMEPAD_INPUT_UP] || gp->values[GAMEPAD_INPUT_LEFT] || gp->values[GAMEPAD_INPUT_L];
  bool next = gp->values[GAMEPAD_INPUT_DOWN] || gp->values[GAMEPAD_INPUT_RIGHT] || gp->values[GAMEPAD_INPUT_R];
  if (prev == next) { preview_text_page_hold_reset(); return; }
  int8_t dir = next ? 1 : -1;
  uint32_t now = lv_tick_get();
  if (!s_page_hold_armed || s_page_hold_dir != dir) {
    s_page_hold_armed = true; s_page_hold_dir = dir; s_page_hold_next_ms = now + TEXT_PAGE_HOLD_MS_INITIAL;
    return;
  }
  if ((int32_t)(now - s_page_hold_next_ms) >= 0) {
    text_change_page(dir); s_page_hold_next_ms = now + TEXT_PAGE_HOLD_MS_REPEAT;
  }
}

static bool preview_text_open(const char *path, preview_open_args_t *args) {
  ESP_LOGI(TAG, "Opening text preview: %s", path);
  lv_coord_t top = ui_chrome_body_top() + 2;
  s_page_w = 290; s_page_h = LCD_HEIGHT - top - 22;
  text_doc_t pending;
  if (!preview_text_load(path, &pending, s_page_w, s_page_h)) {
    ESP_LOGE(TAG, "Failed to load text document");
    return false;
  }
  text_doc_free(&s_doc); s_doc = pending;
  ui_chrome_detach(&s_chrome); lv_obj_clean(args->screen); ui_theme_apply_screen(args->screen);
  s_chrome = ui_chrome_create(args->screen, fm_base_name(path));
  s_body_label = lv_label_create(args->screen);
  lv_obj_set_width(s_body_label, s_page_w);
  lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_line_space(s_body_label, TEXT_LINE_SPACE, 0);
  ui_theme_style_label_primary(s_body_label);
  lv_obj_align(s_body_label, LV_ALIGN_TOP_MID, 0, top);
  s_status_label = lv_label_create(args->screen);
  ui_theme_style_label_secondary(s_status_label);
  lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);
  s_backlight_off = false; s_backlight_restore = 70;
  int32_t saved_bl = 70;
  if (settings_load(SettingBacklight, &saved_bl) == 0) s_backlight_restore = (uint8_t)saved_bl;
  text_show_page(); s_active = true;
  return true;
}

static void preview_text_close(void) {
  if (s_backlight_off) lcd_set_brightness(s_backlight_restore);
  ui_chrome_detach(&s_chrome); text_doc_free(&s_doc);
  s_body_label = s_status_label = NULL; s_active = false;
}

static bool preview_text_on_key(const input_gamepad_state *gp, const bool edge[]) {
  if (!s_active) return false;
  if (edge[GAMEPAD_INPUT_MENU]) {
    if (s_backlight_off) { lcd_set_brightness(s_backlight_restore); s_backlight_off = false; }
    else { lcd_set_brightness(0); s_backlight_off = true; }
    return true;
  }
  if (s_backlight_off) return false;
  if (edge[GAMEPAD_INPUT_UP] || edge[GAMEPAD_INPUT_LEFT] || edge[GAMEPAD_INPUT_L]) { text_change_page(-1); return true; }
  if (edge[GAMEPAD_INPUT_DOWN] || edge[GAMEPAD_INPUT_RIGHT] || edge[GAMEPAD_INPUT_R]) { text_change_page(1); return true; }
  preview_text_page_hold_tick(gp); return false;
}

const preview_app_t preview_text_app = { .id = "text", .can_open = preview_text_can_open, .open = preview_text_open, .close = preview_text_close, .on_key = preview_text_on_key };

