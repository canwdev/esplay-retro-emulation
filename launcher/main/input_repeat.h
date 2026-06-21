#ifndef INPUT_REPEAT_H
#define INPUT_REPEAT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t initial_delay_ms;
  uint32_t repeat_delay_ms;
  uint16_t accel_every;
  uint16_t max_scale;
} input_repeat_config_t;

typedef struct {
  bool armed;
  uint32_t dir;
  uint32_t next_ms;
  uint16_t repeat_count;
} input_repeat_state_t;

void input_repeat_reset(input_repeat_state_t *state);
void input_repeat_arm(input_repeat_state_t *state, uint32_t dir,
                      uint32_t now_ms, const input_repeat_config_t *config);
bool input_repeat_tick(input_repeat_state_t *state, bool active, uint32_t dir,
                       uint32_t now_ms,
                       const input_repeat_config_t *config,
                       uint16_t *out_repeat_count);
int input_repeat_scale_for_count(const input_repeat_config_t *config,
                                 uint16_t repeat_count);

#endif
