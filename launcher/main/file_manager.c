#include "file_manager.h"
#include "audio.h"
#include "input_bridge.h"
#include "preview.h"
#include "ui_app.h"
#include "ui_chrome.h"
#include "ui_home.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
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
/* Long-press auto-scroll timing (ms). */
#define FM_HOLD_MS_INITIAL 400
#define FM_HOLD_MS_REPEAT  80

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
static bool s_hold_armed;
static uint32_t s_hold_dir;
static uint32_t s_hold_next_ms;

static void fm_prompt_delete(const char *fullpath);
static void fm_show_context_menu(const char *fullpath);
static void fm_track_mbox(lv_obj_t *mbox);
static void fm_style_dialog_btn(lv_obj_t *btn);
static void fm_apply_dialog_font(lv_obj_t *obj);
static const char *fm_entry_symbol(const char *name, bool is_dir);
static void fm_virtual_sync_rows(void);
static void fm_update_status_label(void);
static bool fm_build_path(char *out, size_t out_sz, const char *name);

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
                                    lv_obj_t **out_first_btn) {
  lv_obj_t *list = lv_list_create(content);
  lv_obj_set_width(list, lv_pct(100));
  lv_obj_set_height(list, LV_SIZE_CONTENT);
  ui_theme_style_list(list);

  lv_obj_t *first = NULL;
  for (size_t i = 0; i < count; i++) {
    lv_obj_t *btn =
        lv_list_add_btn(list, items[i].symbol, items[i].text);
    fm_style_dialog_btn(btn);
    lv_obj_add_event_cb(btn, fm_dialog_item_event_handler, LV_EVENT_KEY, NULL);
    if (items[i].on_click)
      lv_obj_add_event_cb(btn, items[i].on_click, LV_EVENT_CLICKED, NULL);
    if (!first)
      first = btn;
  }

  if (out_first_btn)
    *out_first_btn = first;
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
  if (strncmp(out, "/sd", 3) != 0)
    return false;
  return true;
}

static void fm_go_parent(void) {
  fm_normalize_cwd();
  if (strcmp(fm_cwd, "/sd") == 0)
    return;
  char *slash = strrchr(fm_cwd, '/');
  if (slash && slash != fm_cwd)
    *slash = '\0';
  else
    strlcpy(fm_cwd, "/sd", sizeof(fm_cwd));
  fm_normalize_cwd();
  if (strlen(fm_cwd) < 3)
    strlcpy(fm_cwd, "/sd", sizeof(fm_cwd));
}

void fm_reset_cwd(void) {
  strlcpy(fm_cwd, "/sd", sizeof(fm_cwd));
  fm_normalize_cwd();
  fm_restore_focus = false;
}

const char *fm_get_cwd(void) { return fm_cwd; }

const char *fm_base_name(const char *path) {
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
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
  audio_stop_playback();
  input_bridge_block_enter_until_release();
  ui_home_create();
}

static void fm_move_focus(int delta) {
  if (s_logical_count <= 0)
    return;
  int next = s_focus_idx + delta;
  if (next < 0)
    next = s_logical_count - 1;
  else if (next >= s_logical_count)
    next = 0;
  s_focus_idx = next;
  fm_virtual_sync_rows();
}

static void fm_hold_reset(void) {
  s_hold_armed = false;
  s_hold_dir   = 0;
  s_hold_next_ms = 0;
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
  if (strcmp(fm_cwd, "/sd") == 0)
    return;
  const char *name = fm_base_name(fm_cwd);
  if (!name || name[0] == '\0')
    return;
  strlcpy(fm_last_focus, name, sizeof(fm_last_focus));
  fm_restore_focus = true;
}

static lv_coord_t fm_font_line_h(lv_obj_t *lbl) {
  const lv_font_t *f = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
  if (!f)
    f = ui_font_default();
  return lv_font_get_line_height(f);
}

static void fm_vrow_apply_single_line(fm_vrow_t *row) {
  lv_coord_t lh = fm_font_line_h(row->text_lbl);
  lv_obj_set_height(row->btn, FM_ROW_H);
  lv_obj_set_width(row->icon_lbl, LV_SIZE_CONTENT);
  lv_obj_set_height(row->icon_lbl, lh);
  lv_obj_remove_flag(row->icon_lbl, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_width(row->text_lbl, s_text_w);
  lv_obj_set_height(row->text_lbl, lh);
  lv_obj_set_style_text_line_space(row->text_lbl, 0, 0);
  lv_obj_set_style_pad_all(row->text_lbl, 0, 0);
  lv_obj_remove_flag(row->text_lbl, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

static void fm_vrow_set_focus(fm_vrow_t *row, bool focused) {
  lv_color_t col = focused ? ui_theme_color_accent() : ui_theme_color_text();
  if (focused) {
    lv_obj_add_state(row->btn, LV_STATE_FOCUSED);
    lv_label_set_long_mode(row->text_lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  } else {
    lv_obj_remove_state(row->btn, LV_STATE_FOCUSED);
    lv_label_set_long_mode(row->text_lbl, LV_LABEL_LONG_MODE_DOTS);
  }
  fm_vrow_apply_single_line(row);
  lv_obj_set_style_text_color(row->icon_lbl, col, 0);
  lv_obj_set_style_text_color(row->text_lbl, col, 0);
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
  lv_obj_add_flag(row->btn, LV_OBJ_FLAG_STATE_TRICKLE);
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
  lv_obj_set_style_text_color(row->icon_lbl, ui_theme_color_text(), 0);
  lv_obj_set_style_text_color(row->text_lbl, ui_theme_color_text(), 0);
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
  const lv_font_t *font = ui_font_builtin();

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
  fm_dialog_add_list(lv_msgbox_get_content(mbox), items, 2, &focus_btn);
  fm_dialog_show(mbox, focus_btn);
}

static void fm_delete_cancel_cb(lv_event_t *e) { fm_dialog_close_cb(e); }

static void fm_delete_confirm_cb(lv_event_t *e) {
  (void)e;
  unlink(fm_delete_path);
  if (fm_open_mbox)
    lv_msgbox_close(fm_open_mbox);
}

static void fm_prompt_delete(const char *fullpath) {
  strlcpy(fm_delete_path, fullpath, sizeof(fm_delete_path));

  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, LV_SYMBOL_TRASH " Delete");
  fm_dialog_add_body(lv_msgbox_get_content(mbox), fm_base_name(fullpath));

  static const fm_dialog_item_t items[] = {
      {LV_SYMBOL_CLOSE, "Cancel", fm_delete_cancel_cb},
      {LV_SYMBOL_TRASH, "Delete", fm_delete_confirm_cb},
  };

  lv_obj_t *focus_btn = NULL;
  fm_dialog_add_list(lv_msgbox_get_content(mbox), items, 2, &focus_btn);
  fm_dialog_show(mbox, focus_btn);
}

static void fm_open_preview(const char *fullpath) {
  fm_remember_focus();

  /* Temporarily free file manager memory to give preview apps enough heap. */
  if (s_entries) {
    free(s_entries);
    s_entries = NULL;
  }

  preview_open_args_t args = {
      .screen = g_ui.screen,
      .input_group = g_ui.input_group,
      .cwd = fm_cwd,
  };

  if (preview_open_for_path(fullpath, &args)) {
    g_ui.current_page = PAGE_FILES;
  } else {
    /* If preview fails to open, restore file manager memory. */
    fm_create();
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

  struct stat st;
  if (stat(full, &st) != 0) {
    ESP_LOGW(TAG, "stat failed for %s", full);
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
}

void fm_on_nav_key(uint32_t key) {
  if (g_ui.current_page != PAGE_FILES || fm_has_open_dialog())
    return;

  if (key == LV_KEY_UP) {
    fm_move_focus(-1);
    s_hold_armed   = true;
    s_hold_dir     = LV_KEY_UP;
    s_hold_next_ms = lv_tick_get() + FM_HOLD_MS_INITIAL;
  } else if (key == LV_KEY_DOWN) {
    fm_move_focus(1);
    s_hold_armed   = true;
    s_hold_dir     = LV_KEY_DOWN;
    s_hold_next_ms = lv_tick_get() + FM_HOLD_MS_INITIAL;
  } else if (key == LV_KEY_LEFT) {
    fm_move_focus(-s_visible_rows);
    s_hold_armed   = true;
    s_hold_dir     = LV_KEY_LEFT;
    s_hold_next_ms = lv_tick_get() + FM_HOLD_MS_INITIAL;
  } else if (key == LV_KEY_RIGHT) {
    fm_move_focus(s_visible_rows);
    s_hold_armed   = true;
    s_hold_dir     = LV_KEY_RIGHT;
    s_hold_next_ms = lv_tick_get() + FM_HOLD_MS_INITIAL;
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
    return;
  }

  uint32_t dir = 0;
  if (up)    dir = LV_KEY_UP;
  if (down)  dir = LV_KEY_DOWN;
  if (left)  dir = LV_KEY_LEFT;
  if (right) dir = LV_KEY_RIGHT;

  if (!s_hold_armed || s_hold_dir != dir) {
    s_hold_armed   = true;
    s_hold_dir     = dir;
    s_hold_next_ms = lv_tick_get() + FM_HOLD_MS_INITIAL;
    return;
  }

  uint32_t now = lv_tick_get();
  if (now < s_hold_next_ms)
    return;

  if (dir == LV_KEY_UP)         fm_move_focus(-1);
  else if (dir == LV_KEY_DOWN)  fm_move_focus(1);
  else if (dir == LV_KEY_LEFT)  fm_move_focus(-s_visible_rows);
  else if (dir == LV_KEY_RIGHT) fm_move_focus(s_visible_rows);

  s_hold_next_ms = now + FM_HOLD_MS_REPEAT;
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

  struct stat st;
  if (stat(full, &st) != 0)
    return;

  fm_show_context_menu(full);
}

void fm_handle_back(void) {
  if (fm_close_top_dialog())
    return;

  if (preview_is_active()) {
    preview_close();
    fm_create();
    return;
  }

  fm_normalize_cwd();
  if (strcmp(fm_cwd, "/sd") != 0) {
    fm_remember_child_for_parent();
    fm_go_parent();
    fm_create();
    return;
  }

  audio_stop_playback();
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
    ESP_LOGE(TAG, "UI state not initialized");
    return;
  }

  /* 1. Close preview first to trigger memory cleanup (like s_playlist and audio stack). */
  if (preview_is_active()) {
    preview_close();
  }

  /* 2. Clean UI objects to free LVGL heap memory. */
  lv_group_remove_all_objs(g_ui.input_group);
  ui_chrome_detach(&s_chrome);
  lv_obj_clean(g_ui.screen);

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
  lv_label_set_long_mode(path_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_width(path_label, panel_w);
  lv_label_set_text(path_label, fm_cwd);
  ui_theme_style_label_secondary(path_label);
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

  s_show_back = (strcmp(fm_cwd, "/sd") == 0);
  s_show_up   = !s_show_back;
  s_open_err  = false;
  s_logical_count = 0;
  if (s_show_back)
    s_logical_count++;
  if (s_show_up)
    s_logical_count++;

  DIR *dir = opendir(fm_cwd);
  if (!dir) {
    ESP_LOGW(TAG, "opendir failed: %s", fm_cwd);
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

    if (want > 0) {
      size_t alloc_sz = want * sizeof(fm_entry_t);
      s_entries = malloc(alloc_sz);
      if (!s_entries) {
        ESP_LOGE(TAG,
                 "Failed to allocate %u bytes for %u entries (Free Heap: %u, Max Block: %u)",
                 (unsigned)alloc_sz, (unsigned)want,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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
  }

  if (fm_restore_focus) {
    s_focus_idx = fm_logical_index_by_name(fm_last_focus);
    fm_restore_focus = false;
  } else {
    /* Default: skip nav row (Back or ..) and land on the first entry. */
    s_focus_idx = (s_logical_count > 1) ? 1 : 0;
  }
  s_window_start = 0;   /* fm_virtual_sync_rows centres it */

  fm_status_pos_label = lv_label_create(g_ui.screen);
  ui_theme_style_label_secondary(fm_status_pos_label);
  lv_obj_align(fm_status_pos_label, LV_ALIGN_BOTTOM_LEFT, 8, -2);

  fm_status_size_label = lv_label_create(g_ui.screen);
  ui_theme_style_label_secondary(fm_status_size_label);
  lv_obj_align(fm_status_size_label, LV_ALIGN_BOTTOM_RIGHT, -8, -2);

  fm_virtual_sync_rows();

  fm_hold_reset();

  if (disp) {
    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(g_ui.screen);
  }

  g_ui.current_page = PAGE_FILES;
}
