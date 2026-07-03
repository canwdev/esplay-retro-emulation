#pragma once

#include <stdbool.h>
#include <stdint.h>

void hal_audio_init(void);
bool hal_audio_is_playing(void);
void hal_audio_play_file(const char *path);
void hal_audio_stop(void);
void hal_audio_set_volume(uint8_t pct);
uint8_t hal_audio_get_volume(void);
void hal_audio_set_eq_preset(int preset);
int  hal_audio_get_eq_preset(void);
