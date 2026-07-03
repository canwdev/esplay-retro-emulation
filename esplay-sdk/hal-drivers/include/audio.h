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

/* Software EQ presets (see eq.h for preset list) */
void audio_set_eq_preset(int preset);
int  audio_get_eq_preset(void);

void audio_pause(void);
void audio_resume(void);
void audio_toggle_pause(void);
bool audio_is_paused(void);

void audio_seek_seconds(int delta);
uint32_t audio_get_position_ms(void);
uint32_t audio_get_duration_ms(void);

typedef enum {
  AUDIO_TRACK_TYPE_NONE = 0,
  AUDIO_TRACK_TYPE_WAV,
  AUDIO_TRACK_TYPE_MP3,
} audio_track_type_t;

typedef struct {
  audio_track_type_t type;
  uint32_t sample_rate_hz;
  uint16_t channels;
  uint16_t bits_per_sample;
  uint16_t bitrate_kbps;
  bool     is_float;
  bool     mp3_vbr;
} audio_track_info_t;

bool audio_get_track_info(audio_track_info_t *out);

#ifdef __cplusplus
}
#endif

#endif
