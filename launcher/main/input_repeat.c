#include "input_repeat.h"

void input_repeat_reset(input_repeat_state_t *state) {
  if (!state)
    return;
  state->armed = false;
  state->dir = 0;
  state->next_ms = 0;
  state->repeat_count = 0;
}

void input_repeat_arm(input_repeat_state_t *state, uint32_t dir,
                      uint32_t now_ms, const input_repeat_config_t *config) {
  if (!state || !config)
    return;
  state->armed = true;
  state->dir = dir;
  state->next_ms = now_ms + config->initial_delay_ms;
  state->repeat_count = 0;
}

bool input_repeat_tick(input_repeat_state_t *state, bool active, uint32_t dir,
                       uint32_t now_ms,
                       const input_repeat_config_t *config,
                       uint16_t *out_repeat_count) {
  if (out_repeat_count)
    *out_repeat_count = 0;
  if (!state || !config)
    return false;

  if (!active) {
    input_repeat_reset(state);
    return false;
  }

  if (!state->armed || state->dir != dir) {
    input_repeat_arm(state, dir, now_ms, config);
    return false;
  }

  if ((int32_t)(now_ms - state->next_ms) < 0)
    return false;

  state->repeat_count++;
  state->next_ms = now_ms + config->repeat_delay_ms;
  if (out_repeat_count)
    *out_repeat_count = state->repeat_count;
  return true;
}

int input_repeat_scale_for_count(const input_repeat_config_t *config,
                                 uint16_t repeat_count) {
  if (!config || config->max_scale <= 1 || config->accel_every == 0)
    return 1;

  int scale = 1 + (int)(repeat_count / config->accel_every);
  if (scale < 1)
    scale = 1;
  if (scale > (int)config->max_scale)
    scale = (int)config->max_scale;
  return scale;
}
