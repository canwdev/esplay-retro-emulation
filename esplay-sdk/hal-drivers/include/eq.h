/**
 * @file eq.h
 * @brief Software equalizer — biquad-based presets for music playback.
 *
 * Provides a 3-band biquad EQ with factory presets.
 * Coefficients are computed at preset-switch time (not per-sample), so the
 * per-sample hot path is just a few float multiply-adds.
 */

#ifndef EQ_H
#define EQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ types */

typedef enum {
  EQ_PRESET_NORMAL = 0,
  EQ_PRESET_BASS_BOOST,
  EQ_PRESET_TREBLE_BOOST,
  EQ_PRESET_VOCAL,
  EQ_PRESET_ROCK,
  EQ_PRESET_BASS_CUT,
  EQ_PRESET_COUNT,
} eq_preset_t;

/* ------------------------------------------------------------------ API */

/** Set the active EQ preset (rebuilds all biquad coefficients). */
void eq_set_preset(eq_preset_t preset);

/** Return the current preset. */
eq_preset_t eq_get_preset(void);

/** Cycle to the next preset (wraps around). */
void eq_next_preset(void);

/** Human-readable short name for a preset (e.g. "Bass+"). */
const char *eq_preset_name(eq_preset_t preset);

/**
 * Process a block of interleaved stereo int16_t samples in-place.
 * If the current preset is NORMAL this is a no-op.
 *
 * @param samples  interleaved L/R samples (samples[0]=L, samples[1]=R, …)
 * @param count    total number of int16_t values (must be even)
 */
void eq_process_block(int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* EQ_H */
