#include "hal_audio.h"

#include "audio.h"

void hal_audio_init(void) {
  audio_init();
}

bool hal_audio_is_playing(void) {
  return audio_is_playing();
}

void hal_audio_play_file(const char *path) {
  audio_play_file_async(path);
}

void hal_audio_stop(void) {
  audio_stop_playback();
}

void hal_audio_set_volume(uint8_t pct) {
  audio_set_volume(pct);
}

uint8_t hal_audio_get_volume(void) {
  return audio_get_volume();
}
