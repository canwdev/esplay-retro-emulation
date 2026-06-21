#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "lvgl.h"
#include <stdint.h>

void ui_settings_load_persisted(void);
void ui_settings_create(void);
void ui_settings_handle_back(void);
/** Drop cached widget pointers after lv_obj_clean(g_ui.screen). */
void ui_settings_detach_ui(void);
void ui_settings_sync_volume(uint8_t volume);
void ui_settings_on_nav_key(uint32_t lv_key);
void ui_settings_on_nav_hold_tick(bool up, bool down, bool left, bool right);
bool ui_settings_uses_direct_nav(void);

#endif
