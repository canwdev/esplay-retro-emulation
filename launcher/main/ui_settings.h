#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <stdint.h>

void ui_settings_load_persisted(void);
void ui_settings_create(void);
void ui_settings_handle_back(void);
/** Drop cached widget pointers after lv_obj_clean(g_ui.screen). */
void ui_settings_detach_ui(void);
void ui_settings_sync_volume(uint8_t volume);

#endif
