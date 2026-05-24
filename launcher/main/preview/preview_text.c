#include "preview_text.h"
#include "file_manager.h"
#include "text_codec.h"
#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "settings.h"
#include "misc/lv_text_private.h"
#include "lcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

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

static void text_doc_free(text_doc_t *doc) {
  free(doc->utf8);
  free(doc->page_off);
  doc->utf8 = NULL;
  doc->page_off = NULL;
  doc->utf8_len = 0;
  doc->page_count = 0;
  doc->cur_page = 0;
}

static int text_build_pages(text_doc_t *doc, lv_coord_t max_w, lv_coord_t max_h) {
  const lv_font_t *font = ui_font_default();
  lv_coord_t line_step = lv_font_get_line_height(font) + TEXT_LINE_SPACE;
  if (line_step < 1)
    line_step = 14 + TEXT_LINE_SPACE;
  int max_lines = max_h / line_step;
  if (max_lines < 1)
    max_lines = 1;

  lv_text_attributes_t attr;
  lv_text_attributes_init(&attr);
  attr.max_width = max_w;
  attr.line_space = TEXT_LINE_SPACE;
  attr.letter_space = 0;
  attr.text_flags = LV_TEXT_FLAG_NONE;

  int cap = 64;
  uint32_t *off = malloc((size_t)cap * sizeof(uint32_t));
  if (!off)
    return -1;

  int pages = 0;
  size_t pos = 0;
  const char *txt = doc->utf8;
  const size_t total = doc->utf8_len;

  while (pos <= total) {
    if (pages >= cap) {
      cap *= 2;
      uint32_t *n = realloc(off, (size_t)cap * sizeof(uint32_t));
      if (!n) {
        free(off);
        return -1;
      }
      off = n;
    }
    off[pages++] = (uint32_t)pos;
    if (pos >= total)
      break;

    int lines = 0;
    while (pos < total && lines < max_lines) {
      if (txt[pos] == '\n') {
        pos++;
        lines++;
        continue;
      }
      uint32_t remain = (uint32_t)(total - pos);
      uint32_t adv =
          lv_text_get_next_line(&txt[pos], remain, font, NULL, &attr);
      if (adv == 0) {
        pos++;
        lines++;
        break;
      }
      pos += adv;
      lines++;
    }
    if (pages > 1 && off[pages - 1] == off[pages - 2]) {
      if (pos < total)
        pos++;
      else
        break;
    }
  }

  if (pages < 2)
    pages = 1;

  doc->page_off = off;
  doc->page_count = pages;
  doc->cur_page = 0;
  return 0;
}

static void text_show_page(void) {
  if (!s_body_label || !lv_obj_is_valid(s_body_label) || !s_doc.utf8)
    return;

  if (s_doc.page_count <= 0) {
    lv_label_set_text(s_body_label, "");
    return;
  }

  if (s_doc.cur_page < 0)
    s_doc.cur_page = 0;
  if (s_doc.cur_page >= s_doc.page_count)
    s_doc.cur_page = s_doc.page_count - 1;

  uint32_t start = s_doc.page_off[s_doc.cur_page];
  uint32_t end = (s_doc.cur_page + 1 < s_doc.page_count)
                     ? s_doc.page_off[s_doc.cur_page + 1]
                     : (uint32_t)s_doc.utf8_len;

  static char page_buf[TEXT_PAGE_MAX];
  size_t n = end - start;
  if (n >= sizeof(page_buf))
    n = sizeof(page_buf) - 1;
  memcpy(page_buf, s_doc.utf8 + start, n);
  page_buf[n] = '\0';
  lv_label_set_text(s_body_label, page_buf);

  if (s_status_label && lv_obj_is_valid(s_status_label)) {
    int pct = 0;
    if (s_doc.page_count > 1)
      pct = (s_doc.cur_page * 100) / (s_doc.page_count - 1);
    lv_label_set_text_fmt(s_status_label, "%d/%d  %d%%", s_doc.cur_page + 1,
                          s_doc.page_count, pct);
  }
}

static bool text_has_ext(const char *path, const char *ext) {
  const char *dot = strrchr(path, '.');
  return dot != NULL && strcasecmp(dot, ext) == 0;
}

static bool preview_text_can_open(const char *path) {
  const char *base = fm_base_name(path);
  if (fm_is_playable_audio_filename(base))
    return false;

  static const char *exts[] = {
      ".txt",  ".text", ".md",   ".json", ".xml",  ".yaml", ".yml",  ".ini",
      ".cfg",  ".log",  ".c",    ".h",    ".cpp",  ".hpp",  ".py",
      ".js",   ".ts",   ".sh",   ".cmake", ".csv", ".html", ".htm",
      ".css",  ".lrc",  ".nfo",
  };
  for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
    if (text_has_ext(path, exts[i]))
      return true;
  }

  const char *dot = strrchr(base, '.');
  if (dot && dot[1] != '\0')
    return false;

  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
    return false;
  if (st.st_size == 0 || (size_t)st.st_size > TEXT_RAW_MAX)
    return false;
  return true;
}

static bool preview_text_load(const char *path, text_doc_t *doc,
                              lv_coord_t page_w, lv_coord_t page_h) {
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
    return false;

  size_t to_read = (size_t)st.st_size;
  if (to_read > TEXT_RAW_MAX)
    to_read = TEXT_RAW_MAX;

  FILE *f = fopen(path, "rb");
  if (!f)
    return false;

  uint8_t *raw = malloc(to_read > 0 ? to_read : 1);
  if (!raw) {
    fclose(f);
    return false;
  }

  size_t n = fread(raw, 1, to_read, f);
  fclose(f);

  memset(doc, 0, sizeof(*doc));
  text_encoding_t enc;
  char *utf8 = text_decode_to_utf8(raw, n, &doc->utf8_len, &enc);
  free(raw);
  if (!utf8)
    return false;

  doc->utf8 = utf8;
  if (text_build_pages(doc, page_w, page_h) != 0) {
    text_doc_free(doc);
    return false;
  }
  return true;
}

static void preview_text_page_hold_reset(void) {
  s_page_hold_armed = false;
  s_page_hold_dir   = 0;
}

static void preview_text_arm_page_hold(int8_t dir) {
  s_page_hold_armed   = true;
  s_page_hold_dir     = dir;
  s_page_hold_next_ms = lv_tick_get() + TEXT_PAGE_HOLD_MS_INITIAL;
}

static void text_change_page(int delta);

static void preview_text_page_hold_tick(const input_gamepad_state *gp) {
  bool prev = gp->values[GAMEPAD_INPUT_UP] == 1 ||
              gp->values[GAMEPAD_INPUT_LEFT] == 1 ||
              gp->values[GAMEPAD_INPUT_L] == 1;
  bool next = gp->values[GAMEPAD_INPUT_DOWN] == 1 ||
              gp->values[GAMEPAD_INPUT_RIGHT] == 1 ||
              gp->values[GAMEPAD_INPUT_R] == 1;

  if ((prev && next) || (!prev && !next)) {
    preview_text_page_hold_reset();
    return;
  }

  int8_t dir = next ? 1 : -1;
  uint32_t now = lv_tick_get();

  if (!s_page_hold_armed || s_page_hold_dir != dir) {
    s_page_hold_armed   = true;
    s_page_hold_dir     = dir;
    s_page_hold_next_ms = now + TEXT_PAGE_HOLD_MS_INITIAL;
    return;
  }

  if ((int32_t)(now - s_page_hold_next_ms) < 0)
    return;

  text_change_page(dir);
  s_page_hold_next_ms = now + TEXT_PAGE_HOLD_MS_REPEAT;
}

static bool preview_text_open(const char *path, preview_open_args_t *args) {
  lv_coord_t body_top = ui_chrome_body_top() + 2;
  s_page_w = 290;
  s_page_h = LCD_HEIGHT - body_top - 22;

  text_doc_t pending;
  if (!preview_text_load(path, &pending, s_page_w, s_page_h))
    return false;

  text_doc_free(&s_doc);
  s_doc = pending;

  ui_chrome_detach(&s_chrome);
  lv_obj_clean(args->screen);
  ui_theme_apply_screen(args->screen);

  s_chrome = ui_chrome_create(args->screen, fm_base_name(path));

  s_body_label = lv_label_create(args->screen);
  lv_obj_set_width(s_body_label, s_page_w);
  lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_line_space(s_body_label, TEXT_LINE_SPACE, 0);
  ui_theme_style_label_primary(s_body_label);
  lv_obj_align(s_body_label, LV_ALIGN_TOP_MID, 0, body_top);

  s_status_label = lv_label_create(args->screen);
  ui_theme_style_label_secondary(s_status_label);
  lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);

  s_backlight_off     = false;
  s_backlight_restore = 70;
  preview_text_page_hold_reset();
  int32_t saved_bl = 70;
  if (settings_load(SettingBacklight, &saved_bl) == 0) {
    if (saved_bl < 10)
      saved_bl = 10;
    if (saved_bl > 100)
      saved_bl = 100;
    s_backlight_restore = (uint8_t)saved_bl;
  }

  text_show_page();
  s_active = true;
  return true;
}

static void preview_text_close(void) {
  if (s_backlight_off) {
    lcd_set_brightness(s_backlight_restore);
    s_backlight_off = false;
  }
  ui_chrome_detach(&s_chrome);
  text_doc_free(&s_doc);
  s_body_label = NULL;
  s_status_label = NULL;
  preview_text_page_hold_reset();
  s_active = false;
}

static void text_change_page(int delta) {
  if (s_doc.page_count <= 0)
    return;
  int next = s_doc.cur_page + delta;
  if (next < 0)
    next = 0;
  if (next >= s_doc.page_count)
    next = s_doc.page_count - 1;
  if (next == s_doc.cur_page)
    return;
  s_doc.cur_page = next;
  text_show_page();
}

static bool preview_text_page_prev_edge(const bool edge[]) {
  return edge[GAMEPAD_INPUT_UP] || edge[GAMEPAD_INPUT_LEFT] ||
         edge[GAMEPAD_INPUT_L];
}

static bool preview_text_page_next_edge(const bool edge[]) {
  return edge[GAMEPAD_INPUT_DOWN] || edge[GAMEPAD_INPUT_RIGHT] ||
         edge[GAMEPAD_INPUT_R];
}

static bool preview_text_on_key(const input_gamepad_state *gp,
                                const bool edge[]) {
  if (!s_active)
    return false;

  if (edge[GAMEPAD_INPUT_MENU]) {
    if (s_backlight_off) {
      lcd_set_brightness(s_backlight_restore);
      s_backlight_off = false;
    } else {
      int32_t saved_bl = s_backlight_restore;
      if (settings_load(SettingBacklight, &saved_bl) == 0) {
        if (saved_bl >= 10 && saved_bl <= 100)
          s_backlight_restore = (uint8_t)saved_bl;
      }
      lcd_set_brightness(0);
      s_backlight_off = true;
    }
    preview_text_page_hold_reset();
    return true;
  }

  if (s_backlight_off)
    return false;

  if (preview_text_page_prev_edge(edge)) {
    text_change_page(-1);
    preview_text_arm_page_hold(-1);
    return true;
  }
  if (preview_text_page_next_edge(edge)) {
    text_change_page(1);
    preview_text_arm_page_hold(1);
    return true;
  }

  preview_text_page_hold_tick(gp);
  return false;
}

static void preview_text_on_timer(void) {}

const preview_app_t preview_text_app = {
    .id = "text",
    .can_open = preview_text_can_open,
    .open = preview_text_open,
    .close = preview_text_close,
    .on_key = preview_text_on_key,
    .on_timer = preview_text_on_timer,
};
