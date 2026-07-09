/**
 * @file eq.c
 * @brief Biquad-based 3-band equalizer with factory presets.
 *
 * Biquad formulas follow the RBJ Audio-EQ-Cookbook.
 * Coefficients are computed once per preset change; the per-sample path is
 * just 5 muls + 4 adds per biquad band.
 */

#include "eq.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ------------------------------------------------------------------ constants */

#define EQ_BANDS      3             /* low / mid / high                */
#define EQ_DEFAULT_FS 44100.0f      /* nominal sample rate for presets */
#define PEAKING_Q     2.0f          /* focused peaking filter Q        */

/* ------------------------------------------------------------------ types */

typedef struct {
  float b0, b1, b2;   /* feed-forward coefficients   */
  float a1, a2;       /* feed-back coefficients (sign from DF I convention:
                         y[n] = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2) */
} biquad_coeff_t;

typedef struct {
  float x1, x2;       /* delayed input  samples      */
  float y1, y2;       /* delayed output samples      */
} biquad_state_t;

/* ------------------------------------------------------------------ static state */

static eq_preset_t    s_preset = EQ_PRESET_NORMAL;
static biquad_coeff_t s_coeff[EQ_BANDS];
static biquad_state_t s_state_l[EQ_BANDS];   /* left  channel per-band history */
static biquad_state_t s_state_r[EQ_BANDS];   /* right channel per-band history */

static const char *s_names[EQ_PRESET_COUNT] = {
    "Normal", "Bass+", "Treble+", "Vocal", "Rock", "Bass Cut",
};

/* ------------------------------------------------------------------ helpers */

static void coeff_passthrough(biquad_coeff_t *c) {
  c->b0 = 1.0f;
  c->b1 = 0.0f;
  c->b2 = 0.0f;
  c->a1 = 0.0f;
  c->a2 = 0.0f;
}

/* ---- RBJ low-shelf --------------------------------------------------- */
static void coeff_low_shelf(biquad_coeff_t *c, float fc, float gain_db,
                            float fs) {
  float w0    = 2.0f * M_PI * fc / fs;
  float cos_w = cosf(w0);
  float sin_w = sinf(w0);
  float A     = powf(10.0f, gain_db / 40.0f);
  float beta  = sqrtf(A);  /* S=1 — steepest flat shelf */

  float b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w + beta * sin_w);
  float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w);
  float b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w - beta * sin_w);
  float a0 = (A + 1.0f) + (A - 1.0f) * cos_w + beta * sin_w;
  float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w);
  float a2 = (A + 1.0f) + (A - 1.0f) * cos_w - beta * sin_w;

  float inv_a0 = 1.0f / a0;
  c->b0 = b0 * inv_a0;
  c->b1 = b1 * inv_a0;
  c->b2 = b2 * inv_a0;
  c->a1 = a1 * inv_a0;
  c->a2 = a2 * inv_a0;
}

/* ---- RBJ high-shelf -------------------------------------------------- */
static void coeff_high_shelf(biquad_coeff_t *c, float fc, float gain_db,
                             float fs) {
  float w0    = 2.0f * M_PI * fc / fs;
  float cos_w = cosf(w0);
  float sin_w = sinf(w0);
  float A     = powf(10.0f, gain_db / 40.0f);
  float beta  = sqrtf(A);  /* S=1 — steepest flat shelf */

  float b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w + beta * sin_w);
  float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w);
  float b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w - beta * sin_w);
  float a0 = (A + 1.0f) - (A - 1.0f) * cos_w + beta * sin_w;
  float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w);
  float a2 = (A + 1.0f) - (A - 1.0f) * cos_w - beta * sin_w;

  float inv_a0 = 1.0f / a0;
  c->b0 = b0 * inv_a0;
  c->b1 = b1 * inv_a0;
  c->b2 = b2 * inv_a0;
  c->a1 = a1 * inv_a0;
  c->a2 = a2 * inv_a0;
}

/* ---- RBJ peaking (band) ---------------------------------------------- */
static void coeff_peaking(biquad_coeff_t *c, float fc, float gain_db,
                          float q, float fs) {
  float w0    = 2.0f * M_PI * fc / fs;
  float cos_w = cosf(w0);
  float sin_w = sinf(w0);
  float A     = powf(10.0f, gain_db / 40.0f);
  float alpha = sin_w / (2.0f * q);

  float b0 = 1.0f + alpha * A;
  float b1 = -2.0f * cos_w;
  float b2 = 1.0f - alpha * A;
  float a0 = 1.0f + alpha / A;
  float a1 = -2.0f * cos_w;
  float a2 = 1.0f - alpha / A;

  float inv_a0 = 1.0f / a0;
  c->b0 = b0 * inv_a0;
  c->b1 = b1 * inv_a0;
  c->b2 = b2 * inv_a0;
  c->a1 = a1 * inv_a0;
  c->a2 = a2 * inv_a0;
}

/* ---- build all bands for a preset ------------------------------------ */
static void eq_build_preset(eq_preset_t preset) {
  float fs = EQ_DEFAULT_FS;

  /* start with all bands pass-through */
  for (int i = 0; i < EQ_BANDS; i++)
    coeff_passthrough(&s_coeff[i]);

  switch (preset) {
  case EQ_PRESET_NORMAL:
    break; /* all pass-through */

  case EQ_PRESET_BASS_BOOST:
    /* Low shelf +6 dB @ 200 Hz */
    coeff_low_shelf(&s_coeff[0], 200.0f, 6.0f, fs);
    break;

  case EQ_PRESET_TREBLE_BOOST:
    /* High shelf +5 dB @ 8 kHz */
    coeff_high_shelf(&s_coeff[2], 8000.0f, 5.0f, fs);
    break;

  case EQ_PRESET_VOCAL:
    /* Peaking +4 dB @ 1.2 kHz, Q = 2.0 */
    coeff_peaking(&s_coeff[1], 1200.0f, 4.0f, PEAKING_Q, fs);
    break;

  case EQ_PRESET_ROCK:
    /* V-shape: low +5 dB @ 200 Hz + high +4 dB @ 6 kHz */
    coeff_low_shelf(&s_coeff[0], 200.0f, 5.0f, fs);
    coeff_high_shelf(&s_coeff[2], 6000.0f, 4.0f, fs);
    break;

  case EQ_PRESET_BASS_CUT:
    /* Low shelf –6 dB @ 200 Hz (protect small speakers) */
    coeff_low_shelf(&s_coeff[0], 200.0f, -6.0f, fs);
    break;

  default:
    break;
  }
}

/* ---- single biquad step (Direct Form I) ------------------------------ */
static inline float biquad_tick(const biquad_coeff_t *c, biquad_state_t *s,
                                float x) {
  float y = c->b0 * x + c->b1 * s->x1 + c->b2 * s->x2
          - c->a1 * s->y1 - c->a2 * s->y2;
  s->x2 = s->x1;
  s->x1 = x;
  s->y2 = s->y1;
  s->y1 = y;
  return y;
}

/* ------------------------------------------------------------------ public */

void eq_set_preset(eq_preset_t preset) {
  if (preset >= EQ_PRESET_COUNT)
    preset = EQ_PRESET_NORMAL;
  s_preset = preset;

  /* Clear state so previous filter ringing doesn't bleed in */
  memset(s_state_l, 0, sizeof(s_state_l));
  memset(s_state_r, 0, sizeof(s_state_r));

  eq_build_preset(preset);
}

eq_preset_t eq_get_preset(void) { return s_preset; }

void eq_next_preset(void) {
  int next = (int)s_preset + 1;
  if (next >= (int)EQ_PRESET_COUNT)
    next = 0;
  eq_set_preset((eq_preset_t)next);
}

const char *eq_preset_name(eq_preset_t preset) {
  if (preset >= EQ_PRESET_COUNT)
    preset = EQ_PRESET_NORMAL;
  return s_names[preset];
}

void eq_process_block(int16_t *samples, size_t count) {
  if (s_preset == EQ_PRESET_NORMAL)
    return;

  for (size_t i = 0; i < count; i++) {
    float y = (float)samples[i];

    for (int b = 0; b < EQ_BANDS; b++) {
      biquad_state_t *st = (i & 1) ? &s_state_r[b] : &s_state_l[b];
      y = biquad_tick(&s_coeff[b], st, y);
    }

    /* hard-clip to int16_t range (should rarely trigger) */
    if (y > 32767.0f)
      y = 32767.0f;
    else if (y < -32768.0f)
      y = -32768.0f;

    samples[i] = (int16_t)y;
  }
}
