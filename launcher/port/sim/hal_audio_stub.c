#include "hal_audio.h"

void hal_audio_init(void) {
}

bool hal_audio_is_playing(void) {
  return false;
}

void hal_audio_play_file(const char *path) {
  (void)path;
}

void hal_audio_stop(void) {
}

void hal_audio_set_volume(uint8_t pct) {
  (void)pct;
}

uint8_t hal_audio_get_volume(void) {
  return 50;
}
