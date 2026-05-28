#include "preview.h"
#include "preview_audio.h"
#include "preview_bmp.h"
#include "preview_text.h"

static const preview_app_t *s_apps[] = {
    &preview_audio_app,
    &preview_bmp_app,
    &preview_text_app,
};

static const preview_app_t *s_active;

bool preview_can_open(const char *path) {
  for (size_t i = 0; i < sizeof(s_apps) / sizeof(s_apps[0]); i++) {
    if (s_apps[i]->can_open(path))
      return true;
  }
  return false;
}

bool preview_open_for_path(const char *path, preview_open_args_t *args) {
  for (size_t i = 0; i < sizeof(s_apps) / sizeof(s_apps[0]); i++) {
    if (s_apps[i]->can_open(path)) {
      if (s_apps[i]->open(path, args)) {
        s_active = s_apps[i];
        return true;
      }
      return false;
    }
  }
  return false;
}

void preview_close(void) {
  if (!s_active)
    return;
  s_active->close();
  s_active = NULL;
}

bool preview_is_active(void) { return s_active != NULL; }

bool preview_on_key(const input_gamepad_state *gp, const bool edge[]) {
  if (!s_active || !s_active->on_key)
    return false;
  return s_active->on_key(gp, edge);
}

void preview_on_timer(void) {
  if (s_active && s_active->on_timer)
    s_active->on_timer();
}
