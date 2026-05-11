/**
 * @file audio.h
 * @brief Minimal I2S WAV playback (16-bit PCM or 32-bit IEEE float → int16).
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);

/** Start playback in a background task. Stops any current playback first. */
void audio_play_wav_async(const char *path);

/** Request stop; playback task exits shortly after. */
void audio_stop_playback(void);

bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif
