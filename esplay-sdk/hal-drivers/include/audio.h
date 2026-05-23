/**
 * @file audio.h
 * @brief I2S playback for WAV and MP3 with volume, pause, seek controls.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);

void audio_play_file_async(const char *path);
void audio_play_wav_async(const char *path);
void audio_stop_playback(void);
bool audio_is_playing(void);

void audio_set_volume(uint8_t pct);
uint8_t audio_get_volume(void);

void audio_pause(void);
void audio_resume(void);
void audio_toggle_pause(void);
bool audio_is_paused(void);

void audio_seek_seconds(int delta);
uint32_t audio_get_position_ms(void);
uint32_t audio_get_duration_ms(void);

#ifdef __cplusplus
}
#endif

#endif
