#include "file_manager.h"
#include "hal_audio.h"
#include "hal_storage.h"
#include "input_repeat.h"
#include "input_bridge.h"
#include "preview.h"
#include "preview_audio.h"
#include "platform_log.h"
#include "platform_mem.h"
#include "ui_app.h"
#include "ui_chrome.h"
#include "ui_home.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "lvgl.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#endif

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "file_manager";

/* Row height in pixels — Vonwaon 12px (13px line height) plus small padding. */
#define FM_ROW_H          20
/* Maximum pre-allocated virtual rows — enough for any supported screen. */
#define FM_MAX_VROWS       10
/* Pixel offset from screen top where the list panel begins. */
#define FM_HEADER_BOTTOM   36
/* Height reserved at the bottom for the file size label. */
#define FM_STATUS_H        18
/* Delay before a focused long filename starts horizontal scrolling. */
#define FM_FOCUS_SCROLL_DELAY_MS 600
/* Approximate bytes that fit in the path label on a 320px display. */
#define FM_PATH_LABEL_BYTES 36
#define FM_NAV_ACCEL_EVERY  4
#define FM_NAV_MAX_SCALE    4

typedef struct {
  lv_obj_t *btn;
  lv_obj_t *icon_lbl;
  lv_obj_t *text_lbl;
} fm_vrow_t;

static char fm_cwd[FM_PATH_MAX];
static char fm_delete_path[FM_PATH_MAX];
static char fm_context_path[FM_PATH_MAX];
static lv_obj_t *fm_status_pos_label;
static lv_obj_t *fm_status_size_label;
static lv_obj_t *fm_open_mbox;
static lv_obj_t *s_list_panel;
static fm_vrow_t s_vrows[FM_MAX_VROWS];
static int       s_visible_rows;   /* computed at runtime from screen height */
static int       s_text_w;         /* label width for DOTS/SCROLL modes      */
static char fm_last_focus[FM_NAME_LEN];
static bool fm_restore_focus;
static bool fm_refresh_on_mbox_close = true;
static fm_entry_t *s_entries = NULL;
static size_t s_entry_count;
static int s_logical_count;
static int s_focus_idx;
static int s_window_start;
static bool s_show_back;   /* "Back" row — root (/sd) only              */
static bool s_show_up;     /* ".." row — subdirectories only            */
static ui_chrome_t s_chrome;
static bool s_open_err;
static input_repeat_state_t s_nav_repeat;
static bool s_focus_scroll_active;
static uint32_t s_focus_scroll_due_ms;

static const input_repeat_config_t s_fm_nav_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = FM_NAV_ACCEL_EVERY,
    .max_scale = FM_NAV_MAX_SCALE,
};

static void fm_prompt_delete(const char *fullpath);
static void fm_show_context_menu(const char *fullpath);
static void fm_track_mbox(lv_obj_t *mbox);
static void fm_style_dialog_btn(lv_obj_t *btn);
static void fm_apply_dialog_font(lv_obj_t *obj);
static bool fm_path_exists(const char *path);
static bool fm_remove_path(const char *path);
static void fm_format_tail_path(char *out, size_t out_sz, const char *path);
static const char *fm_entry_symbol(const char *name, bool is_dir);
static void fm_virtual_sync_rows(void);
static void fm_update_status_label(void);
static bool fm_build_path(char *out, size_t out_sz, const char *name);
static bool fm_is_bmp_filename(const char *name);
typedef bool (*fm_preview_filter_fn)(const char *name);
static fm_preview_filter_fn fm_preview_filter_for_name(const char *name);
static char *fm_build_preview_shared_list(fm_preview_filter_fn filter,
                                          const char *selected_name,
                                          int *out_count, int *out_index);

static const char *fm_root(void) {
  const char *root = hal_storage_root();
  return (root && root[0]) ? root : "/sd";
}

static bool fm_is_root(void) {
  return strcmp(fm_cwd, fm_root()) == 0;
}

static bool fm_has_root_prefix(const char *path) {
  const char *root = fm_root();
  size_t root_len = strlen(root);
  if (strncmp(path, root, root_len) != 0)
    return false;
  if (path[root_len] == '\0')
    return true;
  return path[root_len] == '/';
}

static bool fm_dirent_get_type(const struct dirent *de, bool *out_is_dir) {
  if (!de || !out_is_dir)
    return false;
  if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
    return false;

  if (de->d_type == DT_DIR) {
    *out_is_dir = true;
    return true;
  }
  if (de->d_type == DT_REG) {
    *out_is_dir = false;
    return true;
  }
  if (de->d_type != DT_UNKNOWN)
    return false;

  char probe[FM_PATH_MAX];
  if (!fm_build_path(probe, sizeof(probe), de->d_name))
    return false;

  struct stat st;
  if (stat(probe, &st) != 0)
    return false;
  if (S_ISDIR(st.st_mode)) {
    *out_is_dir = true;
    return true;
  }
  if (S_ISREG(st.st_mode)) {
    *out_is_dir = false;
    return true;
  }
  return false;
}

typedef struct {
  const char *symbol;
  const char *text;
  lv_event_cb_t on_click;
} fm_dialog_item_t;

static void fm_dialog_item_event_handler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;

  uint32_t key = lv_indev_get_key(lv_indev_get_act());
  if (key == LV_KEY_DOWN)
    lv_group_focus_next(lv_group_get_default());
  else if (key == LV_KEY_UP)
    lv_group_focus_prev(lv_group_get_default());
}

static lv_obj_t *fm_dialog_add_body(lv_obj_t *content, const char *text) {
  lv_obj_t *lbl = lv_label_create(content);
  lv_label_set_text(lbl, text);
  lv_obj_set_width(lbl, lv_pct(100));
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
  ui_theme_style_label_primary(lbl);
  lv_obj_set_style_pad_bottom(lbl, 6, 0);
  return lbl;
}

static lv_obj_t *fm_dialog_add_list(lv_obj_t *content,
                                    const fm_dialog_item_t *items, size_t count,
                                    size_t focus_index,
                                    lv_obj_t **out_focus_btn) {
  lv_obj_t *list = lv_list_create(content);
  lv_obj_set_width(list, lv_pct(100));
  lv_obj_set_height(list, LV_SIZE_CONTENT);
  ui_theme_style_list(list);

  lv_obj_t *focus_btn = NULL;
  for (size_t i = 0; i < count; i++) {
    lv_obj_t *btn =
        lv_list_add_btn(list, items[i].symbol, items[i].text);
    fm_style_dialog_btn(btn);
    lv_obj_add_event_cb(btn, fm_dialog_item_event_handler, LV_EVENT_KEY, NULL);
    if (items[i].on_click)
      lv_obj_add_event_cb(btn, items[i].on_click, LV_EVENT_CLICKED, NULL);
    if (i == focus_index)
      focus_btn = btn;
  }

  if (!focus_btn && lv_obj_get_child_cnt(list) > 0)
    focus_btn = lv_obj_get_child(list, 0);
  if (out_focus_btn)
    *out_focus_btn = focus_btn;
  return list;
}

static void fm_dialog_show(lv_obj_t *mbox, lv_obj_t *focus_btn) {
  fm_track_mbox(mbox);
  lv_group_remove_all_objs(g_ui.input_group);
  lv_obj_t *content = lv_msgbox_get_content(mbox);
  if (content) {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(content); i++) {
      lv_obj_t *child = lv_obj_get_child(content, i);
      if (!lv_obj_check_type(child, &lv_list_class))
        continue;
      for (uint32_t j = 0; j < lv_obj_get_child_cnt(child); j++) {
        lv_obj_t *btn = lv_obj_get_child(child, j);
        if (lv_obj_check_type(btn, &lv_list_button_class))
          lv_group_add_obj(g_ui.input_group, btn);
      }
    }
  }
  if (focus_btn)
    lv_group_focus_obj(focus_btn);
  lv_obj_center(mbox);
}

static void fm_normalize_cwd(void) {
  size_t n = strlen(fm_cwd);
  while (n > 1 && fm_cwd[n - 1] == '/') {
    fm_cwd[n - 1] = '\0';
    n--;
  }
}

static bool fm_build_path(char *out, size_t out_sz, const char *name) {
  if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return false;
  if (strchr(name, '/'))
    return false;
  int n = snprintf(out, out_sz, "%s/%s", fm_cwd, name);
  if (n < 0 || (size_t)n >= out_sz)
    return false;
  if (!fm_has_root_prefix(out))
    return false;
  return true;
}

static void fm_go_parent(void) {
  fm_normalize_cwd();
  if (fm_is_root())
    return;
  char *slash = strrchr(fm_cwd, '/');
  if (slash && slash != fm_cwd)
    *slash = '\0';
  else
    strlcpy(fm_cwd, fm_root(), sizeof(fm_cwd));
  fm_normalize_cwd();
  if (!fm_has_root_prefix(fm_cwd))
    strlcpy(fm_cwd, fm_root(), sizeof(fm_cwd));
}

void fm_reset_cwd(void) {
  strlcpy(fm_cwd, fm_root(), sizeof(fm_cwd));
  fm_normalize_cwd();
  fm_restore_focus = false;
}

const char *fm_get_cwd(void) { return fm_cwd; }

const char *fm_base_name(const char *path) {
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

static const char *fm_utf8_char_boundary(const char *p, const char *end) {
  while (p < end && ((*p & 0xC0) == 0x80))
    p++;
  return p;
}

static void fm_format_tail_path(char *out, size_t out_sz, const char *path) {
  if (!out || out_sz == 0)
    return;
  if (!path)
    path = "";

  size_t len = strlen(path);
  if (len <= FM_PATH_LABEL_BYTES) {
    strlcpy(out, path, out_sz);
    return;
  }

  size_t tail_bytes = FM_PATH_LABEL_BYTES - 4; /* ".../" */
  const char *tail = path + len - tail_bytes;
  const char *slash = strchr(tail, '/');
  if (slash && slash[1] != '\0')
    tail = slash + 1;
  else
    tail = fm_utf8_char_boundary(tail, path + len);

  strlcpy(out, ".../", out_sz);
  strlcat(out, tail, out_sz);
}

bool fm_is_wav_filename(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot != NULL && strcasecmp(dot, ".wav") == 0;
}

bool fm_is_mp3_filename(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot != NULL && strcasecmp(dot, ".mp3") == 0;
}

bool fm_is_playable_audio_filename(const char *name) {
  return fm_is_wav_filename(name) || fm_is_mp3_filename(name);
}

static bool fm_is_bmp_filename(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot != NULL && strcasecmp(dot, ".bmp") == 0;
}

static fm_preview_filter_fn fm_preview_filter_for_name(const char *name) {
  if (!name)
    return NULL;
  if (fm_is_playable_audio_filename(name))
    return fm_is_playable_audio_filename;
  if (fm_is_bmp_filename(name))
    return fm_is_bmp_filename;
  return NULL;
}

static char *fm_build_preview_shared_list(fm_preview_filter_fn filter,
                                          const char *selected_name,
                                          int *out_count, int *out_index) {
  int count = 0;
  int index = -1;
  char *names = NULL;

  if (out_count)
    *out_count = 0;
  if (out_index)
    *out_index = -1;
  if (!filter || !selected_name || !s_entries || s_entry_count == 0)
    return NULL;

  for (size_t i = 0; i < s_entry_count; i++) {
    if (!s_entries[i].is_dir && filter(s_entries[i].name))
      count++;
  }
  if (count <= 0)
    return NULL;

  names = (char *)platform_malloc((size_t)count * FM_NAME_LEN);
  if (!names)
    return NULL;

  count = 0;
  for (size_t i = 0; i < s_entry_count; i++) {
    if (s_entries[i].is_dir || !filter(s_entries[i].name))
      continue;
    strlcpy(names + (size_t)count * FM_NAME_LEN, s_entries[i].name, FM_NAME_LEN);
    if (strcmp(s_entries[i].name, selected_name) == 0)
      index = count;
    count++;
  }

  if (count <= 0 || index < 0) {
    platform_free(names);
    return NULL;
  }

  if (out_count)
    *out_count = count;
  if (out_index)
    *out_index = index;
  return names;
}

int fm_entry_compare(const void *a, const void *b) {
  const fm_entry_t *ea = (const fm_entry_t *)a;
  const fm_entry_t *eb = (const fm_entry_t *)b;
  if (ea->is_dir != eb->is_dir)
    return eb->is_dir - ea->is_dir;
  return strcasecmp(ea->name, eb->name);
}

bool fm_has_open_dialog(void) { return fm_open_mbox != NULL; }

bool fm_close_top_dialog(void) {
  if (!fm_open_mbox)
    return false;
  lv_msgbox_close(fm_open_mbox);
  return true;
}

static bool fm_logical_item(int idx, const char **out_name, const char **out_sym,
                            int *out_entry_idx) {
  if (out_entry_idx)
    *out_entry_idx = -1;
  if (!out_name || !out_sym || idx < 0 || idx >= s_logical_count)
    return false;

  int p = 0;
  if (s_show_back && idx == p++) {
    *out_name = "Back";
    *out_sym  = LV_SYMBOL_LEFT;
    return true;
  }
  if (s_show_up && idx == p++) {
    *out_name = "..";
    *out_sym  = LV_SYMBOL_LEFT;
    return true;
  }
  if (s_open_err && idx == p++) {
    *out_name = "(open error)";
    *out_sym  = LV_SYMBOL_WARNING;
    return true;
  }

  int e = idx - p;
  if (e >= 0 && (size_t)e < s_entry_count) {
    *out_name = s_entries[e].name;
    *out_sym  = fm_entry_symbol(s_entries[e].name, s_entries[e].is_dir);
    if (out_entry_idx)
      *out_entry_idx = e;
    return true;
  }
  return false;
}

static int fm_logical_index_by_name(const char *name) {
  if (!name || name[0] == '\0')
    return 0;
  for (int i = 0; i < s_logical_count; i++) {
    const char *n = NULL;
    const char *s = NULL;
    if (fm_logical_item(i, &n, &s, NULL) && n && strcmp(n, name) == 0)
      return i;
  }
  return 0;
}

static void fm_go_home(void) {
  input_bridge_block_enter_until_release();
  ui_home_create();
}

static void fm_focus_scroll_reset(void) {
  s_focus_scroll_active = false;
  s_focus_scroll_due_ms = lv_tick_get() + FM_FOCUS_SCROLL_DELAY_MS;
}

static void fm_focus_scroll_tick(void) {
  if (s_focus_scroll_active)
    return;
  if ((int32_t)(lv_tick_get() - s_focus_scroll_due_ms) < 0)
    return;
  s_focus_scroll_active = true;
  fm_virtual_sync_rows();
}

static void fm_move_focus(int delta) {
  if (s_logical_count <= 0)
    return;
  int old = s_focus_idx;
  int next = s_focus_idx + delta;
  if (next < 0)
    next = s_logical_count - 1;
  else if (next >= s_logical_count)
    next = 0;
  s_focus_idx = next;
  if (s_focus_idx != old)
    fm_focus_scroll_reset();
  fm_virtual_sync_rows();
}

static void fm_hold_reset(void) {
  input_repeat_reset(&s_nav_repeat);
}

static void fm_remember_focus(void) {
  const char *name = NULL;
  const char *sym  = NULL;
  if (!fm_logical_item(s_focus_idx, &name, &sym, NULL) || !name || name[0] == '\0')
    return;
  if (strcmp(name, "Back") == 0 || strcmp(name, "(open error)") == 0)
    return;
  strlcpy(fm_last_focus, name, sizeof(fm_last_focus));
  fm_restore_focus = true;
}

static void fm_remember_child_for_parent(void) {
  if (fm_is_root())
    return;
  const char *name = fm_base_name(fm_cwd);
  if (!name || name[0] == '\0')
    return;
  strlcpy(fm_last_focus, name, sizeof(fm_last_focus));
  fm_restore_focus = true;
}

static void fm_vrow_apply_single_line(fm_vrow_t *row) {
  lv_obj_set_height(row->btn, FM_ROW_H);
  lv_obj_set_width(row->icon_lbl, LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(row->icon_lbl, ui_font_builtin(), 0);
  ui_theme_style_label_row(row->icon_lbl, FM_ROW_H);
  lv_obj_set_width(row->text_lbl, s_text_w);
  ui_theme_style_label_row(row->text_lbl, FM_ROW_H);
}

static void fm_vrow_set_focus(fm_vrow_t *row, bool focused) {
  lv_color_t bg = focused ? ui_theme_color_focus_bg() : ui_theme_color_panel();
  lv_color_t fg = focused ? ui_theme_color_accent() : ui_theme_color_text();

  lv_obj_remove_state(row->btn, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_bg_color(row->btn, bg, 0);
  lv_obj_set_style_text_color(row->btn, fg, 0);
  lv_obj_set_style_text_color(row->icon_lbl, fg, 0);
  lv_obj_set_style_text_color(row->text_lbl, fg, 0);

  if (focused && s_focus_scroll_active) {
    lv_label_set_long_mode(row->text_lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  } else {
    lv_label_set_long_mode(row->text_lbl, LV_LABEL_LONG_MODE_DOTS);
  }
  fm_vrow_apply_single_line(row);
}

static void fm_virtual_sync_rows(void) {
  if (s_focus_idx < 0)
    s_focus_idx = 0;
  if (s_logical_count > 0 && s_focus_idx >= s_logical_count)
    s_focus_idx = s_logical_count - 1;

  /* Keep focused item in the centre of the visible window. */
  int half  = s_visible_rows / 2;
  int ideal = s_focus_idx - half;
  if (ideal < 0)
    ideal = 0;
  int max_start = s_logical_count - s_visible_rows;
  if (max_start < 0)
    max_start = 0;
  if (ideal > max_start)
    ideal = max_start;
  s_window_start = ideal;

  for (int r = 0; r < s_visible_rows; r++) {
    fm_vrow_t *row   = &s_vrows[r];
    int        logic = s_window_start + r;
    if (logic >= s_logical_count) {
      lv_obj_add_flag(row->btn, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const char *name = NULL;
    const char *sym  = NULL;
    if (!fm_logical_item(logic, &name, &sym, NULL)) {
      lv_obj_add_flag(row->btn, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_remove_flag(row->btn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(row->icon_lbl, sym ? sym : "");
    lv_label_set_text(row->text_lbl, name);
    fm_vrow_set_focus(row, logic == s_focus_idx);
    fm_vrow_apply_single_line(row);
  }

  if (fm_status_pos_label && lv_obj_is_valid(fm_status_pos_label))
    fm_update_status_label();
}

static void fm_create_vrow(fm_vrow_t *row, lv_obj_t *parent) {
  row->btn = lv_btn_create(parent);
  lv_group_remove_obj(row->btn);
  lv_obj_set_width(row->btn, LV_PCT(100));
  lv_obj_set_height(row->btn, FM_ROW_H);
  lv_obj_remove_flag(row->btn, LV_OBJ_FLAG_STATE_TRICKLE);
  lv_obj_remove_flag(row->btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row->btn, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_flex_flow(row->btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row->btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  ui_theme_style_list_btn(row->btn);
  /* Override theme pad_all(6) — vertical padding would exceed FM_ROW_H. */
  lv_obj_set_style_pad_top(row->btn, 0, 0);
  lv_obj_set_style_pad_bottom(row->btn, 0, 0);
  lv_obj_set_style_pad_left(row->btn, 6, 0);
  lv_obj_set_style_pad_right(row->btn, 6, 0);
  lv_obj_set_style_pad_column(row->btn, 4, 0);

  row->icon_lbl = lv_label_create(row->btn);
  row->text_lbl = lv_label_create(row->btn);
  lv_label_set_long_mode(row->text_lbl, LV_LABEL_LONG_MODE_DOTS);
  fm_vrow_apply_single_line(row);
  fm_vrow_set_focus(row, false);
}

static void fm_format_size(char *buf, size_t buf_sz, unsigned long bytes) {
  if (bytes >= 1048576UL)
    snprintf(buf, buf_sz, "%lu.%01lu MB", bytes / 1048576UL,
             (bytes % 1048576UL) * 10UL / 1048576UL);
  else if (bytes >= 1024UL)
    snprintf(buf, buf_sz, "%lu KB", bytes / 1024UL);
  else
    snprintf(buf, buf_sz, "%lu B", bytes);
}

static void fm_update_status_label(void) {
  if (fm_status_pos_label && lv_obj_is_valid(fm_status_pos_label)) {
    if (s_logical_count > 0)
      lv_label_set_text_fmt(fm_status_pos_label, "%d/%d", s_focus_idx + 1,
                            s_logical_count);
    else
      lv_label_set_text(fm_status_pos_label, "");
  }

  if (!fm_status_size_label || !lv_obj_is_valid(fm_status_size_label))
    return;

  const char *name = NULL;
  const char *sym  = NULL;
  if (!fm_logical_item(s_focus_idx, &name, &sym, NULL) || !name) {
    lv_label_set_text(fm_status_size_label, "");
    return;
  }

  if (strcmp(name, "Back") == 0 || strcmp(name, "..") == 0 ||
      strcmp(name, "(open error)") == 0) {
    lv_label_set_text(fm_status_size_label, "");
    return;
  }

  char full[FM_PATH_MAX];
  if (!fm_build_path(full, sizeof(full), name)) {
    lv_label_set_text(fm_status_size_label, "");
    return;
  }

#ifdef TARGET_SIM
  bool is_dir = false;
  bool is_reg = false;
  unsigned long size = 0;
  unsigned long winerr = 0;
  if (!sim_path_get_info_utf8(full, &is_dir, &is_reg, &size, &winerr)) {
    platform_log(PLATFORM_LOG_WARN, TAG, "stat failed for %s (winerr=%lu)", full,
                 winerr);
    lv_label_set_text(fm_status_size_label, "");
    return;
  }

  if (is_dir) {
    lv_label_set_text(fm_status_size_label, "Folder");
    return;
  }

  if (is_reg) {
    char sz[32];
    fm_format_size(sz, sizeof(sz), (unsigned long)size);
    lv_label_set_text(fm_status_size_label, sz);
    return;
  }

  lv_label_set_text(fm_status_size_label, "");
  return;
#else
  struct stat st;
  if (stat(full, &st) != 0) {
    lv_label_set_text(fm_status_size_label, "");
    return;
  }

  if (S_ISDIR(st.st_mode)) {
    lv_label_set_text(fm_status_size_label, "Folder");
    return;
  }

  char sz[32];
  fm_format_size(sz, sizeof(sz), (unsigned long)st.st_size);
  lv_label_set_text(fm_status_size_label, sz);
#endif
}

static void fm_mbox_closed_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE)
    return;
  fm_open_mbox = NULL;
  if (fm_refresh_on_mbox_close && !preview_is_active())
    fm_create();
}

static void fm_style_dialog_btn(lv_obj_t *btn) {
  ui_theme_style_list_btn(btn);
}

static void fm_apply_dialog_font(lv_obj_t *obj) {
  const lv_font_t *font = ui_font_default();

  if (lv_obj_check_type(obj, &lv_label_class) ||
      lv_obj_check_type(obj, &lv_list_button_class) ||
      lv_obj_check_type(obj, &lv_button_class))
    lv_obj_set_style_text_font(obj, font, 0);

  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++)
    fm_apply_dialog_font(lv_obj_get_child(obj, i));
}

static void fm_track_mbox(lv_obj_t *mbox) {
  fm_open_mbox = mbox;
  ui_theme_apply_msgbox(mbox);
  fm_apply_dialog_font(mbox);
  lv_obj_add_event_cb(mbox, fm_mbox_closed_cb, LV_EVENT_DELETE, NULL);
}

static bool fm_path_exists(const char *path) {
#ifdef TARGET_SIM
  unsigned long winerr = 0;
  if (!sim_path_get_info_utf8(path, NULL, NULL, NULL, &winerr)) {
    platform_log(PLATFORM_LOG_WARN, TAG, "path info failed for %s (winerr=%lu)",
                 path, winerr);
    return false;
  }
  return true;
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    platform_log(PLATFORM_LOG_WARN, TAG, "stat failed for %s", path);
    return false;
  }
  return true;
#endif
}

static bool fm_remove_path(const char *path) {
  if (!path || path[0] == '\0' || strcmp(path, fm_root()) == 0 ||
      !fm_has_root_prefix(path))
    return false;

#ifdef TARGET_SIM
  unsigned long winerr = 0;
  if (!sim_delete_utf8(path, &winerr)) {
    platform_log(PLATFORM_LOG_WARN, TAG, "delete failed: %s (winerr=%lu)",
                 path, winerr);
    return false;
  }
  return true;
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    platform_log(PLATFORM_LOG_WARN, TAG, "stat failed before delete: %s", path);
    return false;
  }

  if (!S_ISDIR(st.st_mode)) {
    if (unlink(path) != 0) {
      platform_log(PLATFORM_LOG_WARN, TAG, "unlink failed: %s", path);
      return false;
    }
    return true;
  }

  DIR *dir = opendir(path);
  if (!dir) {
    platform_log(PLATFORM_LOG_WARN, TAG, "opendir failed before delete: %s",
                 path);
    return false;
  }

  bool ok = true;
  struct dirent *de;
  while ((de = readdir(dir)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;

    char child[FM_PATH_MAX];
    strlcpy(child, path, sizeof(child));
    strlcat(child, "/", sizeof(child));
    if (strlcat(child, de->d_name, sizeof(child)) >= sizeof(child)) {
      ok = false;
      break;
    }
    if (!fm_remove_path(child)) {
      ok = false;
      break;
    }
  }
  closedir(dir);

  if (!ok)
    return false;
  if (rmdir(path) != 0) {
    platform_log(PLATFORM_LOG_WARN, TAG, "rmdir failed: %s", path);
    return false;
  }
  return true;
#endif
}

static void fm_dialog_close_cb(lv_event_t *e) {
  (void)e;
  if (fm_open_mbox)
    lv_msgbox_close(fm_open_mbox);
}

static void fm_menu_cancel_cb(lv_event_t *e) { fm_dialog_close_cb(e); }

static void fm_menu_delete_cb(lv_event_t *e) {
  (void)e;
  fm_refresh_on_mbox_close = false;
  if (fm_open_mbox)
    lv_msgbox_close(fm_open_mbox);
  fm_refresh_on_mbox_close = true;
  fm_prompt_delete(fm_context_path);
}

static void fm_show_context_menu(const char *fullpath) {
  strlcpy(fm_context_path, fullpath, sizeof(fm_context_path));
  fm_remember_focus();

  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, fm_base_name(fullpath));

  static const fm_dialog_item_t items[] = {
      {LV_SYMBOL_TRASH, "Delete", fm_menu_delete_cb},
      {LV_SYMBOL_CLOSE, "Cancel", fm_menu_cancel_cb},
  };

  lv_obj_t *focus_btn = NULL;
  fm_dialog_add_list(lv_msgbox_get_content(mbox), items, 2, 1, &focus_btn);
  fm_dialog_show(mbox, focus_btn);
}

static void fm_delete_cancel_cb(lv_event_t *e) { fm_dialog_close_cb(e); }

static void fm_delete_confirm_cb(lv_event_t *e) {
  (void)e;
  fm_remove_path(fm_delete_path);
  if (fm_open_mbox)
    lv_msgbox_close(fm_open_mbox);
}

static void fm_prompt_delete(const char *fullpath) {
  strlcpy(fm_delete_path, fullpath, sizeof(fm_delete_path));

  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, "Confirm Delete");
  fm_dialog_add_body(lv_msgbox_get_content(mbox), fm_base_name(fullpath));

  static const fm_dialog_item_t items[] = {
      {LV_SYMBOL_CLOSE, "Cancel", fm_delete_cancel_cb},
      {LV_SYMBOL_TRASH, "Delete", fm_delete_confirm_cb},
  };

  lv_obj_t *focus_btn = NULL;
  fm_dialog_add_list(lv_msgbox_get_content(mbox), items, 2, 1, &focus_btn);
  fm_dialog_show(mbox, focus_btn);
}

static void fm_open_preview(const char *fullpath) {
  fm_remember_focus();
  const char *selected_name = fm_base_name(fullpath);
  bool is_audio = fm_is_playable_audio_filename(selected_name);
  const char *playing_path = preview_audio_session_current_path();
  fm_preview_filter_fn filter = fm_preview_filter_for_name(selected_name);
  char *shared_names = NULL;
  int shared_count = 0;
  int shared_index = -1;

  shared_names = fm_build_preview_shared_list(filter, selected_name, &shared_count,
                                              &shared_index);

  /* Temporarily free file manager memory to give preview apps enough heap. */
  if (s_entries) {
    free(s_entries);
    s_entries = NULL;
  }

  if (is_audio && preview_audio_session_is_active() && playing_path &&
      strcmp(playing_path, fullpath) == 0) {
    platform_free(shared_names);
    if (preview_audio_restore_foreground(g_ui.screen, g_ui.input_group))
      g_ui.current_page = PAGE_FILES;
    else
      fm_create();
    return;
  }

  preview_open_args_t args = {
      .screen = g_ui.screen,
      .input_group = g_ui.input_group,
      .cwd = fm_cwd,
      .shared_names = shared_names,
      .shared_count = shared_count,
      .shared_index = shared_index,
      .shared_name_stride = FM_NAME_LEN,
  };

  if (preview_open_for_path(fullpath, &args)) {
    g_ui.current_page = PAGE_FILES;
  } else {
    bool retried_without_music = false;
    if (!is_audio && preview_audio_session_is_active()) {
      preview_audio_stop_session();
      retried_without_music = preview_open_for_path(fullpath, &args);
      if (retried_without_music)
        g_ui.current_page = PAGE_FILES;
    }
    if (!retried_without_music) {
      platform_free(shared_names);
      /* If preview fails to open, restore file manager memory. */
      fm_create();
    }
  }
}

static void fm_open_by_name(const char *name) {
  if (!name)
    return;

  if (strcmp(name, "Back") == 0) {
    fm_go_home();
    return;
  }
  if (strcmp(name, "..") == 0) {
    fm_remember_child_for_parent();
    fm_go_parent();
    fm_create();
    return;
  }
  if (strcmp(name, "(open error)") == 0)
    return;

  char full[FM_PATH_MAX];
  if (!fm_build_path(full, sizeof(full), name))
    return;

#ifdef TARGET_SIM
  bool is_dir = false;
  bool is_reg = false;
  unsigned long size = 0;
  unsigned long winerr = 0;
  if (!sim_path_get_info_utf8(full, &is_dir, &is_reg, &size, &winerr)) {
    platform_log(PLATFORM_LOG_WARN, TAG, "stat failed for %s (winerr=%lu)", full,
                 winerr);
    return;
  }

  if (is_dir) {
    strlcpy(fm_cwd, full, sizeof(fm_cwd));
    fm_normalize_cwd();
    fm_restore_focus = false;
    fm_create();
    return;
  }

  if (is_reg) {
    if (preview_can_open(full))
      fm_open_preview(full);
  }
  return;
#else
  struct stat st;
  if (stat(full, &st) != 0) {
    platform_log(PLATFORM_LOG_WARN, TAG, "stat failed for %s", full);
    return;
  }

  if (S_ISDIR(st.st_mode)) {
    strlcpy(fm_cwd, full, sizeof(fm_cwd));
    fm_normalize_cwd();
    fm_restore_focus = false;
    fm_create();
    return;
  }

  if (S_ISREG(st.st_mode)) {
    if (preview_can_open(full))
      fm_open_preview(full);
  }
#endif
}

void fm_on_nav_key(uint32_t key) {
  if (g_ui.current_page != PAGE_FILES || fm_has_open_dialog())
    return;

  if (key == LV_KEY_UP) {
    fm_move_focus(-1);
    input_repeat_arm(&s_nav_repeat, LV_KEY_UP, lv_tick_get(), &s_fm_nav_repeat);
  } else if (key == LV_KEY_DOWN) {
    fm_move_focus(1);
    input_repeat_arm(&s_nav_repeat, LV_KEY_DOWN, lv_tick_get(),
                     &s_fm_nav_repeat);
  } else if (key == LV_KEY_LEFT) {
    fm_move_focus(-s_visible_rows);
    input_repeat_arm(&s_nav_repeat, LV_KEY_LEFT, lv_tick_get(),
                     &s_fm_nav_repeat);
  } else if (key == LV_KEY_RIGHT) {
    fm_move_focus(s_visible_rows);
    input_repeat_arm(&s_nav_repeat, LV_KEY_RIGHT, lv_tick_get(),
                     &s_fm_nav_repeat);
  } else if (key == LV_KEY_ENTER) {
    const char *name = NULL;
    const char *sym  = NULL;
    if (fm_logical_item(s_focus_idx, &name, &sym, NULL))
      fm_open_by_name(name);
  }
}

void fm_on_nav_hold_tick(bool up, bool down, bool left, bool right) {
  if (g_ui.current_page != PAGE_FILES || fm_has_open_dialog()) {
    fm_hold_reset();
    return;
  }

  int held_count = (up ? 1 : 0) + (down ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
  if (held_count != 1) {
    fm_hold_reset();
    fm_focus_scroll_tick();
    return;
  }

  uint32_t dir = 0;
  if (up)    dir = LV_KEY_UP;
  if (down)  dir = LV_KEY_DOWN;
  if (left)  dir = LV_KEY_LEFT;
  if (right) dir = LV_KEY_RIGHT;

  uint32_t now = lv_tick_get();
  uint16_t repeat_count = 0;
  if (!input_repeat_tick(&s_nav_repeat, true, dir, now, &s_fm_nav_repeat,
                         &repeat_count))
    return;

  int scale = input_repeat_scale_for_count(&s_fm_nav_repeat, repeat_count);
  if (dir == LV_KEY_UP)         fm_move_focus(-scale);
  else if (dir == LV_KEY_DOWN)  fm_move_focus(scale);
  else if (dir == LV_KEY_LEFT)  fm_move_focus(-s_visible_rows * scale);
  else if (dir == LV_KEY_RIGHT) fm_move_focus(s_visible_rows * scale);
}

bool fm_uses_direct_nav(void) {
  return g_ui.current_page == PAGE_FILES && !fm_has_open_dialog();
}

void fm_handle_menu_on_focus(void) {
  if (fm_has_open_dialog() || preview_is_active())
    return;

  const char *name = NULL;
  const char *sym  = NULL;
  if (!fm_logical_item(s_focus_idx, &name, &sym, NULL) || !name)
    return;
  if (strcmp(name, "Back") == 0 || strcmp(name, "..") == 0)
    return;
  if (strcmp(name, "(open error)") == 0)
    return;

  char full[FM_PATH_MAX];
  if (!fm_build_path(full, sizeof(full), name))
    return;

  if (!fm_path_exists(full))
    return;

  fm_show_context_menu(full);
}

void fm_handle_back(void) {
  if (fm_close_top_dialog())
    return;

  if (preview_is_active()) {
    const char *path = preview_current_path();
    const char *name = path ? fm_base_name(path) : NULL;
    if (name && name[0]) {
      strlcpy(fm_last_focus, name, sizeof(fm_last_focus));
      fm_restore_focus = true;
    }
    preview_close();
    fm_create();
    return;
  }

  fm_normalize_cwd();
  if (!fm_is_root()) {
    fm_remember_child_for_parent();
    fm_go_parent();
    fm_create();
    return;
  }
  fm_go_home();
}

static const char *fm_entry_symbol(const char *name, bool is_dir) {
  if (is_dir)
    return LV_SYMBOL_DIRECTORY;
  if (fm_is_playable_audio_filename(name))
    return LV_SYMBOL_AUDIO;

  char full[FM_PATH_MAX];
  if (fm_build_path(full, sizeof(full), name) && preview_can_open(full))
    return LV_SYMBOL_LIST;

  return LV_SYMBOL_FILE;
}

void fm_create(void) {
  if (!g_ui.input_group || !g_ui.screen) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "UI state not initialized");
    return;
  }
  platform_log(PLATFORM_LOG_INFO, TAG, "fm_create cwd=%s", fm_cwd[0] ? fm_cwd : "(empty)");

  /* 1. Close preview first to trigger memory cleanup (like s_playlist and audio stack). */
  if (preview_is_active()) {
    preview_close();
  }

  /* 2. Clean UI objects to free LVGL heap memory. */
  lv_group_remove_all_objs(g_ui.input_group);
  ui_chrome_detach(&s_chrome);
  lv_obj_clean(g_ui.screen);
  platform_log(PLATFORM_LOG_DEBUG, TAG, "screen cleaned");

  if (s_entries) {
    free(s_entries);
    s_entries = NULL;
  }

  fm_normalize_cwd();
  s_entry_count = 0;

  lv_display_t *disp = lv_display_get_default();
  if (disp)
    lv_display_enable_invalidation(disp, false);

  /* Compute layout dimensions from actual screen size. */
  int screen_w = disp ? lv_display_get_horizontal_resolution(disp) : 320;
  int screen_h = disp ? lv_display_get_vertical_resolution(disp)   : 240;
  int panel_w  = screen_w - 20;   /* 10 px margin each side */
  int list_h   = screen_h - FM_HEADER_BOTTOM - FM_STATUS_H;
  s_visible_rows = list_h / FM_ROW_H;
  if (s_visible_rows < 1)          s_visible_rows = 1;
  if (s_visible_rows > FM_MAX_VROWS) s_visible_rows = FM_MAX_VROWS;
  /* text_w: panel_w minus button pad_hor(6×2) minus icon(18) minus gap(4). */
  s_text_w = panel_w - 12 - 18 - 4;
  if (s_text_w < 40) s_text_w = 40;

  ui_theme_apply_screen(g_ui.screen);

  s_chrome = ui_chrome_create(g_ui.screen, "File Manager");

  lv_obj_t *path_label = lv_label_create(g_ui.screen);
  char path_display[FM_PATH_MAX];
  fm_format_tail_path(path_display, sizeof(path_display), fm_cwd);
  lv_label_set_text(path_label, path_display);
  ui_theme_style_label_secondary(path_label);
  ui_theme_style_label_truncated(path_label, panel_w);
  lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top());

  s_list_panel = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(s_list_panel);
  ui_theme_style_panel(s_list_panel);
  lv_obj_set_size(s_list_panel, panel_w, s_visible_rows * FM_ROW_H);
  lv_obj_align(s_list_panel, LV_ALIGN_TOP_MID, 0, FM_HEADER_BOTTOM);
  lv_obj_set_flex_flow(s_list_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_list_panel, 0, 0);
  lv_obj_set_style_pad_all(s_list_panel, 0, 0);
  lv_obj_set_style_border_width(s_list_panel, 0, 0);
  lv_obj_remove_flag(s_list_panel, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < s_visible_rows; i++)
    fm_create_vrow(&s_vrows[i], s_list_panel);

  s_show_back = fm_is_root();
  s_show_up   = !s_show_back;
  s_open_err  = false;
  s_logical_count = 0;
  if (s_show_back)
    s_logical_count++;
  if (s_show_up)
    s_logical_count++;

  platform_log(PLATFORM_LOG_INFO, TAG, "opening dir: %s", fm_cwd);
  DIR *dir = opendir(fm_cwd);
  if (!dir) {
    platform_log(PLATFORM_LOG_WARN, TAG, "opendir failed: %s", fm_cwd);
    s_open_err = true;
    s_logical_count++;
  } else {
    size_t want = 0;
    struct dirent *de;
    bool is_dir;
    while ((de = readdir(dir)) != NULL && want < FM_MAX_ENTRIES) {
      if (fm_dirent_get_type(de, &is_dir))
        want++;
    }
    platform_log(PLATFORM_LOG_INFO, TAG, "dir entries=%u", (unsigned)want);

    if (want > 0) {
      size_t alloc_sz = want * sizeof(fm_entry_t);
      s_entries = malloc(alloc_sz);
      if (!s_entries) {
        platform_log(
            PLATFORM_LOG_ERROR, TAG,
            "Failed to allocate %u bytes for %u entries (Free Heap: %u, Max Block: %u)",
            (unsigned)alloc_sz, (unsigned)want, (unsigned)platform_free_heap(),
            (unsigned)platform_largest_free_block());
        closedir(dir);
        return;
      }
      rewinddir(dir);
    }

    size_t n = 0;
    while (s_entries && (de = readdir(dir)) != NULL && n < want) {
      if (!fm_dirent_get_type(de, &is_dir))
        continue;
      strlcpy(s_entries[n].name, de->d_name, FM_NAME_LEN);
      s_entries[n].is_dir = is_dir;
      n++;
    }
    closedir(dir);
    s_entry_count = n;
    if (s_entries && n > 0)
      qsort(s_entries, n, sizeof(s_entries[0]), fm_entry_compare);
    s_logical_count += (int)n;
    platform_log(PLATFORM_LOG_INFO, TAG, "loaded entries=%u", (unsigned)n);
  }

  if (fm_restore_focus) {
    s_focus_idx = fm_logical_index_by_name(fm_last_focus);
    fm_restore_focus = false;
  } else {
    /* Default: skip nav row (Back or ..) and land on the first entry. */
    s_focus_idx = (s_logical_count > 1) ? 1 : 0;
  }
  s_window_start = 0;   /* fm_virtual_sync_rows centres it */
  fm_focus_scroll_reset();

  fm_status_pos_label = lv_label_create(g_ui.screen);
  ui_theme_style_label_secondary(fm_status_pos_label);
  lv_obj_align(fm_status_pos_label, LV_ALIGN_BOTTOM_LEFT, 8, -2);

  fm_status_size_label = lv_label_create(g_ui.screen);
  ui_theme_style_label_secondary(fm_status_size_label);
  lv_obj_align(fm_status_size_label, LV_ALIGN_BOTTOM_RIGHT, -8, -2);

  fm_virtual_sync_rows();
  platform_log(PLATFORM_LOG_DEBUG, TAG, "virtual rows synced");

  fm_hold_reset();

  if (disp) {
    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(g_ui.screen);
  }

  g_ui.current_page = PAGE_FILES;
}
