#ifndef PREVIEW_H
#define PREVIEW_H

#include "hal_input.h"
#include "lvgl.h"
#include <stdbool.h>

typedef struct {
  lv_obj_t *screen;
  lv_group_t *input_group;
  const char *cwd;
} preview_open_args_t;

typedef struct preview_app {
  const char *id;
  bool (*can_open)(const char *path);
  bool (*open)(const char *path, preview_open_args_t *args);
  void (*close)(void);
  bool (*on_key)(const input_gamepad_state *gp, const bool edge[]);
  void (*on_timer)(void);
} preview_app_t;

bool preview_can_open(const char *path);
bool preview_open_for_path(const char *path, preview_open_args_t *args);
void preview_close(void);
bool preview_is_active(void);
bool preview_on_key(const input_gamepad_state *gp, const bool edge[]);
void preview_on_timer(void);

#endif
