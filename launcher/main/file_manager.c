#include "file_manager.h"
#include "audio.h"
#include "preview.h"
#include "ui_app.h"
#include "ui_home.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "lvgl.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "file_manager";

static char fm_cwd[FM_PATH_MAX];
static char fm_delete_path[FM_PATH_MAX];
static char fm_context_path[FM_PATH_MAX];
static lv_obj_t *fm_status_label;
static lv_obj_t *fm_open_mbox;
static lv_obj_t *fm_focus_list;
static char fm_last_focus[FM_NAME_LEN];
static bool fm_restore_focus;
static bool fm_refresh_on_mbox_close = true;

static void fm_prompt_delete(const char *fullpath);
static void fm_show_context_menu(const char *fullpath);
static void fm_track_mbox(lv_obj_t *mbox);
static void fm_style_dialog_btn(lv_obj_t *btn);

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

static bool fm_get_obj_label(lv_obj_t *obj, const char **out_name) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      *out_name = lv_label_get_text(child);
      return *out_name != NULL;
    }
  }
  return false;
}

static void fm_remember_focus(void) {
  lv_obj_t *obj = lv_group_get_focused(g_ui.input_group);
  const char *name = NULL;
  if (!obj || !fm_get_obj_label(obj, &name) || !name || name[0] == '\0')
    return;
  if (strcmp(name, "Back") == 0 || strcmp(name, "(open error)") == 0)
    return;
  strlcpy(fm_last_focus, name, sizeof(fm_last_focus));
  fm_restore_focus = true;
}

static lv_obj_t *fm_find_list_btn_by_name(lv_obj_t *list, const char *name) {
  if (!list || !name || name[0] == '\0')
    return NULL;

  for (uint32_t i = 0; i < lv_obj_get_child_cnt(list); i++) {
    lv_obj_t *btn = lv_obj_get_child(list, i);
    const char *btn_name = NULL;
    if (fm_get_obj_label(btn, &btn_name) && btn_name &&
        strcmp(btn_name, name) == 0)
      return btn;
  }
  return NULL;
}

static void fm_focus_initial(lv_obj_t *list, lv_obj_t *btn_back, int focus_count) {
  if (fm_restore_focus) {
    lv_obj_t *btn = fm_find_list_btn_by_name(list, fm_last_focus);
    if (btn) {
      lv_group_focus_obj(btn);
      fm_restore_focus = false;
      return;
    }
    fm_restore_focus = false;
  }

  if (focus_count > 0) {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(list); i++) {
      lv_obj_t *btn = lv_obj_get_child(list, i);
      const char *name = NULL;
      if (fm_get_obj_label(btn, &name) && name && strcmp(name, "Back") != 0) {
        lv_group_focus_obj(btn);
        return;
      }
    }
  }

  lv_group_focus_obj(btn_back);
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

static void fm_mbox_closed_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE)
    return;
  fm_open_mbox = NULL;
  if (fm_refresh_on_mbox_close && !preview_is_active())
    fm_create();
}

static void fm_style_list_btn(lv_obj_t *btn) {
  ui_theme_style_list_btn(btn);
  lv_group_add_obj(g_ui.input_group, btn);
}

static void fm_style_dialog_btn(lv_obj_t *btn) {
  ui_theme_style_list_btn(btn);
}

static void fm_track_mbox(lv_obj_t *mbox) {
  fm_open_mbox = mbox;
  ui_theme_apply_msgbox(mbox);
  lv_obj_add_event_cb(mbox, fm_mbox_closed_cb, LV_EVENT_DELETE, NULL);
}

static void fm_dialog_close_cb(lv_event_t *e) {
  (void)e;
  if (fm_open_mbox)
    lv_msgbox_close(fm_open_mbox);
}

static void fm_show_info(const char *fullpath) {
  struct stat st;
  if (stat(fullpath, &st) != 0)
    return;

  fm_remember_focus();

  char body[384];
  const char *base = fm_base_name(fullpath);

  if (S_ISDIR(st.st_mode)) {
    int count = 0;
    DIR *d = opendir(fullpath);
    if (d) {
      struct dirent *de;
      while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
          count++;
      }
      closedir(d);
    }
    snprintf(body, sizeof(body), "%s\nType: Folder\nItems: %d", base, count);
  } else if (S_ISREG(st.st_mode)) {
    char szbuf[32];
    fm_format_size(szbuf, sizeof(szbuf), (unsigned long)st.st_size);
    const char *type = fm_is_wav_filename(base)    ? "WAV audio"
                     : fm_is_mp3_filename(base)    ? "MP3 audio"
                                                   : "File";
    snprintf(body, sizeof(body), "%s\nType: %s\nSize: %s", base, type, szbuf);
  } else {
    snprintf(body, sizeof(body), "%s\nType: Other", base);
  }

  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, LV_SYMBOL_FILE " Info");
  fm_dialog_add_body(lv_msgbox_get_content(mbox), body);

  static const fm_dialog_item_t items[] = {
      {LV_SYMBOL_CLOSE, "Close", fm_dialog_close_cb},
  };

  lv_obj_t *focus_btn = NULL;
  fm_dialog_add_list(lv_msgbox_get_content(mbox), items, 1, &focus_btn);
  fm_dialog_show(mbox, focus_btn);
}

static void fm_menu_cancel_cb(lv_event_t *e) { fm_dialog_close_cb(e); }

static void fm_menu_info_cb(lv_event_t *e) {
  (void)e;
  fm_refresh_on_mbox_close = false;
  if (fm_open_mbox)
    lv_msgbox_close(fm_open_mbox);
  fm_refresh_on_mbox_close = true;
  fm_show_info(fm_context_path);
}

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
      {LV_SYMBOL_FILE, "Info", fm_menu_info_cb},
      {LV_SYMBOL_TRASH, "Delete", fm_menu_delete_cb},
      {LV_SYMBOL_CLOSE, "Cancel", fm_menu_cancel_cb},
  };

  lv_obj_t *focus_btn = NULL;
  fm_dialog_add_list(lv_msgbox_get_content(mbox), items, 3, &focus_btn);
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
  preview_open_args_t args = {
      .screen = g_ui.screen,
      .input_group = g_ui.input_group,
      .cwd = fm_cwd,
  };
  if (preview_open_for_path(fullpath, &args))
    g_ui.current_page = PAGE_FILES;
  else
    fm_show_info(fullpath);
}

static void fm_handle_open(lv_obj_t *obj) {
  const char *name = NULL;
  if (!fm_get_obj_label(obj, &name))
    return;

  if (strcmp(name, "Back") == 0) {
    audio_stop_playback();
    ui_home_create();
    return;
  }
  if (strcmp(name, "..") == 0) {
    fm_go_parent();
    fm_restore_focus = false;
    fm_create();
    return;
  }

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
    else
      fm_show_info(full);
  }
}

void fm_handle_menu_on_focus(void) {
  if (fm_has_open_dialog() || preview_is_active())
    return;

  lv_obj_t *obj = lv_group_get_focused(g_ui.input_group);
  if (!obj)
    return;

  const char *name = NULL;
  if (!fm_get_obj_label(obj, &name))
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
    fm_go_parent();
    fm_restore_focus = false;
    fm_create();
    return;
  }

  audio_stop_playback();
  ui_home_create();
}

static void file_manager_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_indev_get_key(lv_indev_get_act());
    if (key == LV_KEY_DOWN)
      lv_group_focus_next(lv_group_get_default());
    else if (key == LV_KEY_UP)
      lv_group_focus_prev(lv_group_get_default());
    return;
  }

  if (code != LV_EVENT_CLICKED)
    return;

  fm_handle_open(obj);
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

  preview_close();
  fm_normalize_cwd();

  lv_group_remove_all_objs(g_ui.input_group);
  lv_obj_clean(g_ui.screen);
  ui_theme_apply_screen(g_ui.screen);

  lv_obj_t *title = lv_label_create(g_ui.screen);
  lv_label_set_text(title, "Files");
  ui_theme_style_label_accent(title);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *path_label = lv_label_create(g_ui.screen);
  lv_label_set_long_mode(path_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_width(path_label, 290);
  lv_label_set_text(path_label, fm_cwd);
  ui_theme_style_label_secondary(path_label);
  lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, 18);

  lv_obj_t *list = lv_list_create(g_ui.screen);
  fm_focus_list = list;
  lv_obj_set_size(list, 300, 160);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 38);
  ui_theme_style_list(list);

  lv_obj_t *btn_back = lv_list_add_btn(list, LV_SYMBOL_LEFT, "Back");
  fm_style_list_btn(btn_back);
  lv_obj_add_event_cb(btn_back, file_manager_event_handler, LV_EVENT_ALL, NULL);

  bool not_root = (strcmp(fm_cwd, "/sd") != 0);
  int focus_count = 0;

  if (not_root) {
    lv_obj_t *btn_up = lv_list_add_btn(list, LV_SYMBOL_LEFT, "..");
    fm_style_list_btn(btn_up);
    lv_obj_add_event_cb(btn_up, file_manager_event_handler, LV_EVENT_ALL, NULL);
    focus_count++;
  }

  DIR *dir = opendir(fm_cwd);
  if (!dir) {
    ESP_LOGW(TAG, "opendir failed: %s", fm_cwd);
    lv_obj_t *err = lv_list_add_btn(list, LV_SYMBOL_WARNING, "(open error)");
    fm_style_list_btn(err);
    lv_obj_add_event_cb(err, file_manager_event_handler, LV_EVENT_ALL, NULL);
    focus_count++;
  } else {
    static fm_entry_t entries[FM_MAX_ENTRIES];
    size_t n = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && n < FM_MAX_ENTRIES) {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;
      strlcpy(entries[n].name, de->d_name, FM_NAME_LEN);

      if (de->d_type == DT_DIR) {
        entries[n].is_dir = true;
      } else if (de->d_type == DT_REG) {
        entries[n].is_dir = false;
      } else if (de->d_type == DT_UNKNOWN) {
        char probe[FM_PATH_MAX];
        if (!fm_build_path(probe, sizeof(probe), de->d_name))
          continue;
        struct stat ost;
        if (stat(probe, &ost) != 0)
          continue;
        if (S_ISDIR(ost.st_mode))
          entries[n].is_dir = true;
        else if (S_ISREG(ost.st_mode))
          entries[n].is_dir = false;
        else
          continue;
      } else {
        continue;
      }
      n++;
    }
    closedir(dir);

    qsort(entries, n, sizeof(entries[0]), fm_entry_compare);

    for (size_t i = 0; i < n; i++) {
      const char *sym =
          fm_entry_symbol(entries[i].name, entries[i].is_dir);
      lv_obj_t *row = lv_list_add_btn(list, sym, entries[i].name);
      fm_style_list_btn(row);
      lv_obj_add_event_cb(row, file_manager_event_handler, LV_EVENT_ALL, NULL);
      focus_count++;
    }
  }

  fm_focus_initial(list, btn_back, focus_count);

  fm_status_label = lv_label_create(g_ui.screen);
  lv_label_set_long_mode(fm_status_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_width(fm_status_label, 300);
  lv_label_set_text(fm_status_label, "A:open  B:back  MENU:menu");
  ui_theme_style_label_secondary(fm_status_label);
  lv_obj_align(fm_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);

  g_ui.current_page = PAGE_FILES;
  (void)fm_focus_list;
}
