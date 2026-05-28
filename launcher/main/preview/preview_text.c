#include "preview_text.h"
#include "file_manager.h"
#include "hal_display.h"
#include "platform_log.h"
#include "platform_mem.h"
#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "ui_backlight.h"
#include "misc/lv_text_private.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "../text/gb2312_uni.inc"

static const char *TAG = "preview_text";

#define TEXT_DETECT_SAMPLE     4096
#define TEXT_DECODE_WINDOW     8192
#define TEXT_LINE_SPACE        4
#define TEXT_LETTER_SPACE      1
#define TEXT_PAGE_HOLD_MS_INITIAL 400
#define TEXT_PAGE_HOLD_MS_REPEAT  80
#define TEXT_PAGE_HOLD_FAST_MS    1200
#define TEXT_PAGE_HOLD_FASTER_MS  2400
#define TEXT_PAGE_HOLD_MAX_MS     4000
#define TEXT_PAGE_HOLD_TURBO_MS   7000

typedef enum {
  TEXT_ENC_UTF8 = 0,
  TEXT_ENC_GB2312,
} text_encoding_t;

typedef struct {
  char path[FM_PATH_MAX];
  FILE *file;
  size_t file_size;
  size_t bom_skip;
  text_encoding_t encoding;
  uint32_t *page_off;
  int page_count;
  int page_cap;
  int cur_page;
} text_doc_t;

static text_doc_t s_doc;
static ui_chrome_t s_chrome;
static lv_obj_t *s_body_label;
static lv_obj_t *s_status_label;
static bool s_active;
static bool s_page_hold_armed;
static int8_t s_page_hold_dir;
static uint32_t s_page_hold_start_ms;
static uint32_t s_page_hold_next_ms;

static lv_coord_t s_page_w;
static lv_coord_t s_page_h;

/* ------------------------------------------------------------------ common */

static void *pmalloc(size_t n) {
  return platform_malloc(n);
}

static void pfree(void *p) {
  platform_free(p);
}

static void *prealloc(void *p, size_t old_n, size_t new_n) {
  void *n = platform_malloc(new_n);
  if (!n)
    return NULL;
  if (p && old_n) {
    size_t c = (old_n < new_n) ? old_n : new_n;
    memcpy(n, p, c);
  }
  platform_free(p);
  return n;
}

static void text_copy_path(char *dst, size_t dst_sz, const char *src) {
  if (!dst || dst_sz == 0)
    return;
  if (!src)
    src = "";
  strncpy(dst, src, dst_sz - 1);
  dst[dst_sz - 1] = '\0';
}

static void text_format_size(char *buf, size_t buf_sz, size_t bytes) {
  if (bytes >= 1024 * 1024)
    snprintf(buf, buf_sz, "%u.%uM", (unsigned)(bytes / (1024 * 1024)),
             (unsigned)((bytes % (1024 * 1024)) * 10 / (1024 * 1024)));
  else if (bytes >= 1024)
    snprintf(buf, buf_sz, "%uK", (unsigned)(bytes / 1024));
  else
    snprintf(buf, buf_sz, "%uB", (unsigned)bytes);
}

static FILE *text_fopen(const char *path, const char *mode) {
#ifdef TARGET_SIM
  unsigned long winerr = 0;
  FILE *f = sim_fopen_utf8(path, mode, &winerr);
  if (!f)
    platform_log(PLATFORM_LOG_ERROR, TAG, "fopen failed: %s (winerr=%lu)",
                 path, winerr);
  return f;
#else
  FILE *f = fopen(path, mode);
  if (!f)
    platform_log(PLATFORM_LOG_ERROR, TAG, "fopen failed: %s", path);
  return f;
#endif
}

static bool text_get_file_size(const char *path, size_t *out_size) {
#ifdef TARGET_SIM
  bool is_dir = false;
  bool is_reg = false;
  unsigned long size = 0;
  unsigned long winerr = 0;
  if (!sim_path_get_info_utf8(path, &is_dir, &is_reg, &size, &winerr) ||
      !is_reg || size == 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG,
                 "stat failed or file empty: %s (winerr=%lu)", path, winerr);
    return false;
  }
  *out_size = (size_t)size;
  return true;
#else
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "stat failed or file empty: %s",
                 path);
    return false;
  }
  *out_size = (size_t)st.st_size;
  return true;
#endif
}

/* ------------------------------------------------------------------ codec */

static const char *text_encoding_name(text_encoding_t enc) {
  return enc == TEXT_ENC_GB2312 ? "GB2312" : "UTF-8";
}

static bool utf8_decode_one(const uint8_t *s, size_t len, size_t *adv,
                            uint32_t *cp) {
  if (len == 0)
    return false;

  uint8_t b0 = s[0];
  if (b0 < 0x80) {
    *adv = 1;
    *cp = b0;
    return true;
  }

  size_t need = 0;
  uint32_t val = 0;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    need = 2;
    val = b0 & 0x1F;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    need = 3;
    val = b0 & 0x0F;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    need = 4;
    val = b0 & 0x07;
  } else {
    return false;
  }

  if (len < need)
    return false;

  for (size_t i = 1; i < need; i++) {
    if ((s[i] & 0xC0) != 0x80)
      return false;
    val = (val << 6) | (uint32_t)(s[i] & 0x3F);
  }

  if ((need == 3 && val < 0x800) || (need == 4 && val < 0x10000) ||
      val > 0x10FFFF || (val >= 0xD800 && val <= 0xDFFF))
    return false;

  *adv = need;
  *cp = val;
  return true;
}

static size_t utf8_expected_len(uint8_t b0) {
  if (b0 < 0x80)
    return 1;
  if (b0 >= 0xC2 && b0 <= 0xDF)
    return 2;
  if (b0 >= 0xE0 && b0 <= 0xEF)
    return 3;
  if (b0 >= 0xF0 && b0 <= 0xF4)
    return 4;
  return 0;
}

static size_t utf8_encode(uint32_t cp, char *out) {
  if (cp <= 0x7F) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static bool gb2312_pair(const uint8_t *s, size_t len, uint32_t *cp) {
  if (len < 2 || s[0] < 0xA1 || s[0] > 0xF7 || s[1] < 0xA1 || s[1] > 0xFE)
    return false;

  uint16_t u = gb2312_uni[(s[0] - 0xA1) * 94 + (s[1] - 0xA1)];
  *cp = (u == 0xFFFF) ? 0xFFFD : u;
  return true;
}

static text_encoding_t text_detect_encoding_sample(const uint8_t *s, size_t len,
                                                   size_t *bom_skip) {
  *bom_skip = 0;
  if (len >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) {
    *bom_skip = 3;
    return TEXT_ENC_UTF8;
  }

  size_t ascii = 0;
  size_t utf8_chars = 0;
  size_t gb_pairs = 0;
  size_t bad_utf8 = 0;

  for (size_t i = 0; i < len;) {
    if (s[i] < 0x80) {
      ascii++;
      i++;
      continue;
    }

    size_t adv = 0;
    uint32_t cp = 0;
    size_t need = utf8_expected_len(s[i]);
    if (need > 0 && len - i < need)
      break; /* A fixed-size sample may end in the middle of a UTF-8 char. */
    if (utf8_decode_one(&s[i], len - i, &adv, &cp)) {
      utf8_chars++;
      i += adv;
    } else {
      bad_utf8++;
      i++;
    }
  }

  for (size_t i = 0; i + 1 < len;) {
    uint32_t cp = 0;
    if (s[i] < 0x80) {
      i++;
    } else if (gb2312_pair(&s[i], len - i, &cp)) {
      gb_pairs++;
      i += 2;
    } else {
      i++;
    }
  }

  if (ascii == len)
    return TEXT_ENC_UTF8;
  if (utf8_chars > 0 && bad_utf8 == 0)
    return TEXT_ENC_UTF8;
  if (gb_pairs > 0 && gb_pairs > utf8_chars)
    return TEXT_ENC_GB2312;
  return TEXT_ENC_UTF8;
}

typedef struct {
  char *text;
  uint16_t *raw_after;
  size_t len;
  bool eof;
} text_chunk_t;

static void text_chunk_free(text_chunk_t *chunk) {
  if (!chunk)
    return;
  pfree(chunk->text);
  pfree(chunk->raw_after);
  memset(chunk, 0, sizeof(*chunk));
}

static bool text_decode_raw(text_encoding_t enc, const uint8_t *raw,
                            size_t raw_len, size_t file_start,
                            text_chunk_t *out) {
  size_t cap = (enc == TEXT_ENC_GB2312) ? (raw_len * 3 + 1) : (raw_len + 1);
  out->text = pmalloc(cap + 1);
  out->raw_after = pmalloc((cap + 1) * sizeof(uint16_t));
  if (!out->text || !out->raw_after) {
    pfree(out->text);
    pfree(out->raw_after);
    out->text = NULL;
    out->raw_after = NULL;
    return false;
  }

  size_t o = 0;
  out->raw_after[0] = 0;

  for (size_t i = 0; i < raw_len;) {
    uint32_t cp = 0;
    size_t src_adv = 1;
    char enc_buf[4];
    size_t enc_len = 0;

    if (enc == TEXT_ENC_UTF8) {
      if (!utf8_decode_one(&raw[i], raw_len - i, &src_adv, &cp)) {
        if (i + 4 >= raw_len)
          break;
        cp = '?';
        src_adv = 1;
      }
    } else if (raw[i] < 0x80) {
      cp = raw[i];
      src_adv = 1;
    } else if (gb2312_pair(&raw[i], raw_len - i, &cp)) {
      src_adv = 2;
    } else {
      cp = '?';
      src_adv = 1;
    }

    if (cp == '\r')
      cp = '\n';

    enc_len = utf8_encode(cp, enc_buf);
    if (o + enc_len > cap)
      break;

    for (size_t j = 0; j < enc_len; j++) {
      out->text[o++] = enc_buf[j];
      out->raw_after[o] = (uint16_t)(i + src_adv);
    }
    i += src_adv;
  }

  out->text[o] = '\0';
  out->len = o;
  return true;
}

/* ------------------------------------------------------------------ doc */

static void text_doc_free(text_doc_t *doc) {
  if (doc->file)
    fclose(doc->file);
  pfree(doc->page_off);
  memset(doc, 0, sizeof(*doc));
}

static bool text_doc_push_offset(text_doc_t *doc, size_t off) {
  if (doc->page_count + 1 >= doc->page_cap) {
    int old_cap = doc->page_cap;
    int new_cap = old_cap ? old_cap * 2 : 128;
    uint32_t *n =
        prealloc(doc->page_off, (size_t)old_cap * sizeof(uint32_t),
                 (size_t)new_cap * sizeof(uint32_t));
    if (!n)
      return false;
    doc->page_off = n;
    doc->page_cap = new_cap;
  }
  doc->page_off[doc->page_count++] = (uint32_t)off;
  return true;
}

static bool text_read_raw(const text_doc_t *doc, size_t off, size_t max_len,
                          uint8_t *buf, size_t *out_len, bool *out_eof) {
  if (!doc->file)
    return false;

  if (fseek(doc->file, (long)off, SEEK_SET) != 0)
    return false;

  size_t remain = doc->file_size > off ? doc->file_size - off : 0;
  size_t want = remain < max_len ? remain : max_len;
  size_t n = fread(buf, 1, want, doc->file);

  *out_len = n;
  *out_eof = (off + n) >= doc->file_size;
  return n > 0 || *out_eof;
}

static bool text_decode_at(const text_doc_t *doc, size_t off, size_t max_len,
                           text_chunk_t *chunk) {
  uint8_t *raw = pmalloc(TEXT_DECODE_WINDOW);
  size_t raw_len = 0;
  bool eof = false;
  size_t want = max_len < TEXT_DECODE_WINDOW ? max_len : TEXT_DECODE_WINDOW;

  memset(chunk, 0, sizeof(*chunk));
  if (!raw)
    return false;
  if (!text_read_raw(doc, off, want, raw, &raw_len, &eof)) {
    pfree(raw);
    return false;
  }
  if (!text_decode_raw(doc->encoding, raw, raw_len, off, chunk)) {
    pfree(raw);
    return false;
  }
  pfree(raw);
  chunk->eof = eof;
  return true;
}

static int text_max_lines(lv_coord_t max_h) {
  const lv_font_t *font = ui_font_default();
  lv_coord_t line_h = lv_font_get_line_height(font) + TEXT_LINE_SPACE;
  int max_lines = max_h / (line_h > 0 ? line_h : 14);
  return max_lines < 1 ? 1 : max_lines;
}

static bool text_next_page_offset(const text_doc_t *doc, size_t start,
                                  lv_coord_t max_w, lv_coord_t max_h,
                                  size_t *out_next) {
  if (start >= doc->file_size) {
    *out_next = doc->file_size;
    return true;
  }

  text_chunk_t chunk;
  if (!text_decode_at(doc, start, TEXT_DECODE_WINDOW, &chunk))
    return false;
  if (chunk.len == 0) {
    text_chunk_free(&chunk);
    *out_next = doc->file_size;
    return true;
  }

  const lv_font_t *font = ui_font_default();
  lv_text_attributes_t attr;
  lv_text_attributes_init(&attr);
  attr.max_width = max_w;
  attr.letter_space = TEXT_LETTER_SPACE;
  attr.line_space = TEXT_LINE_SPACE;

  size_t pos = 0;
  int max_lines = text_max_lines(max_h);
  for (int l = 0; l < max_lines && pos < chunk.len; l++) {
    if (chunk.text[pos] == '\n') {
      pos++;
      continue;
    }
    uint32_t adv =
        lv_text_get_next_line(&chunk.text[pos], chunk.len - pos, font, NULL,
                              &attr);
    pos += adv > 0 ? adv : 1;
  }

  uint16_t raw_adv = 0;
  if (pos >= chunk.len && !chunk.eof)
    raw_adv = chunk.raw_after[chunk.len];
  else
    raw_adv = chunk.raw_after[pos < chunk.len ? pos : chunk.len];

  *out_next = start + (size_t)raw_adv;

  if (*out_next <= start)
    *out_next = start + 1;
  if (*out_next > doc->file_size)
    *out_next = doc->file_size;

  text_chunk_free(&chunk);
  return true;
}

static bool text_build_page_index(text_doc_t *doc, lv_coord_t max_w,
                                  lv_coord_t max_h) {
  size_t off = doc->bom_skip;
  if (!text_doc_push_offset(doc, off))
    return false;

  while (off < doc->file_size) {
    size_t next = off;
    if (!text_next_page_offset(doc, off, max_w, max_h, &next))
      return false;
    if (next <= off)
      next = off + 1;
    if (!text_doc_push_offset(doc, next))
      return false;
    off = next;
  }

  doc->page_count -= 1; /* Last offset is sentinel. */
  doc->cur_page = 0;
  platform_log(PLATFORM_LOG_INFO, TAG,
               "indexed %d pages encoding=%s file=%zu window=%u", doc->page_count,
               text_encoding_name(doc->encoding), doc->file_size,
               (unsigned)TEXT_DECODE_WINDOW);
  return doc->page_count > 0;
}

static bool text_decode_page(const text_doc_t *doc, int page, text_chunk_t *out) {
  size_t start = doc->page_off[page];
  size_t end = doc->page_off[page + 1];
  if (end < start)
    return false;

  uint8_t *raw = pmalloc(TEXT_DECODE_WINDOW);
  size_t raw_len = 0;
  bool eof = false;
  if (!raw)
    return false;
  if (!text_read_raw(doc, start, end - start, raw, &raw_len, &eof)) {
    pfree(raw);
    return false;
  }
  (void)eof;
  bool ok = text_decode_raw(doc->encoding, raw, raw_len, start, out);
  pfree(raw);
  return ok;
}

static void text_show_page(void) {
  if (!s_body_label || s_doc.page_count <= 0)
    return;
  if (s_doc.cur_page < 0) s_doc.cur_page = 0;
  if (s_doc.cur_page >= s_doc.page_count) s_doc.cur_page = s_doc.page_count - 1;

  text_chunk_t page;
  if (text_decode_page(&s_doc, s_doc.cur_page, &page)) {
    lv_label_set_text(s_body_label, page.text);
    text_chunk_free(&page);
  } else {
    lv_label_set_text(s_body_label, "(read error)");
  }

  if (s_status_label) {
    int pct = (s_doc.page_count > 1) ? (s_doc.cur_page * 100 / (s_doc.page_count - 1)) : 0;
    char size_buf[16];
    text_format_size(size_buf, sizeof(size_buf), s_doc.file_size);
    lv_label_set_text_fmt(s_status_label, "%s  %d/%d  %d%%  %s",
                          text_encoding_name(s_doc.encoding),
                          s_doc.cur_page + 1, s_doc.page_count, pct, size_buf);
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
#ifdef TARGET_SIM
  bool is_dir = false;
  bool is_reg = false;
  unsigned long size = 0;
  unsigned long winerr = 0;
  if (!sim_path_get_info_utf8(path, &is_dir, &is_reg, &size, &winerr))
    return false;
  return is_reg && size > 0;
#else
  struct stat st;
  return (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);
#endif
}

static bool preview_text_load(const char *path, text_doc_t *doc, lv_coord_t w, lv_coord_t h) {
  memset(doc, 0, sizeof(*doc));
  text_copy_path(doc->path, sizeof(doc->path), path);
  if (!text_get_file_size(path, &doc->file_size))
    return false;

  doc->file = text_fopen(path, "rb");
  if (!doc->file)
    return false;

  uint8_t *sample = pmalloc(TEXT_DETECT_SAMPLE);
  if (!sample) {
    text_doc_free(doc);
    return false;
  }
  size_t sample_len = fread(sample, 1, TEXT_DETECT_SAMPLE, doc->file);

  doc->encoding =
      text_detect_encoding_sample(sample, sample_len, &doc->bom_skip);
  pfree(sample);
  platform_log(PLATFORM_LOG_INFO, TAG,
               "load %s size=%zu encoding=%s bom_skip=%zu heap=%u", path,
               doc->file_size, text_encoding_name(doc->encoding), doc->bom_skip,
               (unsigned)platform_free_heap());

  if (!text_build_page_index(doc, w, h)) {
    text_doc_free(doc);
    return false;
  }
  return true;
}

static void preview_text_page_hold_reset(void) {
  s_page_hold_armed = false;
  s_page_hold_dir = 0;
}

static int preview_text_page_hold_step(uint32_t held_ms) {
  if (held_ms >= TEXT_PAGE_HOLD_TURBO_MS)
    return 100;
  if (held_ms >= TEXT_PAGE_HOLD_MAX_MS)
    return 50;
  if (held_ms >= TEXT_PAGE_HOLD_FASTER_MS)
    return 10;
  if (held_ms >= TEXT_PAGE_HOLD_FAST_MS)
    return 5;
  return 1;
}

static void text_change_page(int delta) {
  if (s_doc.page_count <= 0) return;
  if (delta == -1 && s_doc.cur_page == 0) {
    s_doc.cur_page = s_doc.page_count - 1;
    text_show_page();
    return;
  }
  if (delta == 1 && s_doc.cur_page == s_doc.page_count - 1) {
    s_doc.cur_page = 0;
    text_show_page();
    return;
  }
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
    s_page_hold_armed = true;
    s_page_hold_dir = dir;
    s_page_hold_start_ms = now;
    s_page_hold_next_ms = now + TEXT_PAGE_HOLD_MS_INITIAL;
    return;
  }
  if ((int32_t)(now - s_page_hold_next_ms) >= 0) {
    int step = preview_text_page_hold_step(now - s_page_hold_start_ms);
    text_change_page(dir * step);
    s_page_hold_next_ms = now + TEXT_PAGE_HOLD_MS_REPEAT;
  }
}

static bool preview_text_open(const char *path, preview_open_args_t *args) {
  platform_log(PLATFORM_LOG_INFO, TAG, "Opening text preview: %s", path);
  lv_coord_t top = ui_chrome_body_top() + 2;
  s_page_w = 290;
  s_page_h = HAL_DISPLAY_HEIGHT - top - 22;
  text_doc_t pending;
  if (!preview_text_load(path, &pending, s_page_w, s_page_h)) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "Failed to load text document");
    return false;
  }
  text_doc_free(&s_doc); s_doc = pending;
  ui_chrome_detach(&s_chrome); lv_obj_clean(args->screen); ui_theme_apply_screen(args->screen);
  s_chrome = ui_chrome_create(args->screen, fm_base_name(path));
  s_body_label = lv_label_create(args->screen);
  lv_obj_set_width(s_body_label, s_page_w);
  lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_letter_space(s_body_label, TEXT_LETTER_SPACE, 0);
  lv_obj_set_style_text_line_space(s_body_label, TEXT_LINE_SPACE, 0);
  ui_theme_style_label_primary(s_body_label);
  lv_obj_align(s_body_label, LV_ALIGN_TOP_MID, 0, top);
  s_status_label = lv_label_create(args->screen);
  ui_theme_style_label_secondary(s_status_label);
  lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);
  
  text_show_page(); s_active = true;
  return true;
}

static void preview_text_close(void) {
  ui_chrome_detach(&s_chrome); text_doc_free(&s_doc);
  s_body_label = s_status_label = NULL; s_active = false;
}

static bool preview_text_on_key(const input_gamepad_state *gp, const bool edge[]) {
  if (!s_active) return false;
  if (edge[GAMEPAD_INPUT_MENU]) {
    ui_backlight_toggle();
    return true;
  }
  if (!ui_backlight_is_on()) return false;
  if (edge[GAMEPAD_INPUT_UP] || edge[GAMEPAD_INPUT_LEFT] || edge[GAMEPAD_INPUT_L]) { text_change_page(-1); return true; }
  if (edge[GAMEPAD_INPUT_DOWN] || edge[GAMEPAD_INPUT_RIGHT] || edge[GAMEPAD_INPUT_R]) { text_change_page(1); return true; }
  preview_text_page_hold_tick(gp); return false;
}

const preview_app_t preview_text_app = { .id = "text", .can_open = preview_text_can_open, .open = preview_text_open, .close = preview_text_close, .on_key = preview_text_on_key };

