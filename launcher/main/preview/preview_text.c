#include "preview_text.h"
#include "file_manager.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define TEXT_PREVIEW_MAX 32768

static char s_text_buf[TEXT_PREVIEW_MAX];
static lv_obj_t *s_scroll;
static lv_obj_t *s_label;
static bool s_active;

static bool text_has_ext(const char *path, const char *ext) {
  const char *dot = strrchr(path, '.');
  return dot != NULL && strcasecmp(dot, ext) == 0;
}

static bool preview_text_can_open(const char *path) {
  static const char *exts[] = {
      ".txt",  ".md",   ".json", ".xml",  ".yaml", ".yml",  ".ini",
      ".cfg",  ".log",  ".c",    ".h",    ".cpp",  ".hpp",  ".py",
      ".js",   ".ts",   ".sh",   ".cmake", ".csv",
  };
  for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
    if (text_has_ext(path, exts[i]))
      return true;
  }
  return false;
}

static bool preview_text_open(const char *path, preview_open_args_t *args) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;

  size_t n = fread(s_text_buf, 1, TEXT_PREVIEW_MAX - 64, f);
  fclose(f);
  s_text_buf[n] = '\0';

  if (n >= TEXT_PREVIEW_MAX - 64) {
    strlcat(s_text_buf, "\n...[truncated]", sizeof(s_text_buf));
  }

  for (char *p = s_text_buf; *p; p++) {
    if (*p == '\r')
      *p = '\n';
  }

  lv_obj_clean(args->screen);
  ui_theme_apply_screen(args->screen);

  lv_obj_t *title = lv_label_create(args->screen);
  lv_label_set_text(title, fm_base_name(path));
  ui_theme_style_label_accent(title);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

  s_scroll = lv_obj_create(args->screen);
  lv_obj_set_size(s_scroll, 310, 190);
  lv_obj_align(s_scroll, LV_ALIGN_TOP_MID, 0, 20);
  ui_theme_style_scroll(s_scroll);
  lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_AUTO);

  s_label = lv_label_create(s_scroll);
  lv_label_set_text(s_label, s_text_buf);
  lv_obj_set_width(s_label, 290);
  lv_label_set_long_mode(s_label, LV_LABEL_LONG_MODE_WRAP);
  ui_theme_style_label_primary(s_label);

  lv_obj_t *hint = lv_label_create(args->screen);
  lv_label_set_text(hint, LV_SYMBOL_UP " " LV_SYMBOL_DOWN " scroll   B back");
  ui_theme_style_label_secondary(hint);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

  s_active = true;
  return true;
}

static void preview_text_close(void) {
  s_scroll = NULL;
  s_label = NULL;
  s_active = false;
}

static bool preview_text_on_key(const input_gamepad_state *gp,
                                const bool edge[]) {
  if (!s_active || !s_scroll)
    return false;

  if (edge[GAMEPAD_INPUT_UP]) {
    lv_obj_scroll_by(s_scroll, 0, 24, LV_ANIM_OFF);
    return true;
  }
  if (edge[GAMEPAD_INPUT_DOWN]) {
    lv_obj_scroll_by(s_scroll, 0, -24, LV_ANIM_OFF);
    return true;
  }
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
