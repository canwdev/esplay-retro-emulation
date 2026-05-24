#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>

#define FM_PATH_MAX 256
#define FM_MAX_ENTRIES 96
#define FM_NAME_LEN 128

typedef struct {
  char name[FM_NAME_LEN];
  bool is_dir;
} fm_entry_t;

void fm_reset_cwd(void);
const char *fm_get_cwd(void);
const char *fm_base_name(const char *path);
bool fm_is_wav_filename(const char *name);
bool fm_is_mp3_filename(const char *name);
bool fm_is_playable_audio_filename(const char *name);
int fm_entry_compare(const void *a, const void *b);

void fm_create(void);
void fm_on_nav_key(uint32_t lv_key);
void fm_on_nav_hold_tick(bool up_held, bool down_held);
bool fm_uses_direct_nav(void);
void fm_handle_back(void);
void fm_handle_menu_on_focus(void);
bool fm_close_top_dialog(void);
bool fm_has_open_dialog(void);

#endif
