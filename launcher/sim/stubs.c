#include "preview.h"

bool preview_is_active(void) {
  return false;
}

bool preview_on_key(const input_gamepad_state *gp, const bool edge[]) {
  (void)gp;
  (void)edge;
  return false;
}

void preview_on_timer(void) {
}

bool preview_can_open(const char *path) {
  (void)path;
  return false;
}

bool preview_open_for_path(const char *path, preview_open_args_t *args) {
  (void)path;
  (void)args;
  return false;
}

void preview_close(void) {
}
