#ifndef UI_APP_H
#define UI_APP_H

#include "lvgl.h"

typedef enum {
  PAGE_HOME = 0,
  PAGE_FILES,
  PAGE_SETTINGS,
  PAGE_SCREEN_TEST,
} app_page_t;

typedef struct {
  lv_group_t *input_group;
  lv_obj_t *screen;
  lv_obj_t *home_btn_files;
  lv_obj_t *home_btn_music;
  lv_obj_t *home_btn_settings;
  lv_obj_t *menu_selected_label;
  lv_indev_t *input_device;
  app_page_t current_page;
} ui_state_t;

extern ui_state_t g_ui;

#endif
