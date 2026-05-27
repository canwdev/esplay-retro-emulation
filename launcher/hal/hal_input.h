#pragma once

#include <stdint.h>

#ifdef TARGET_ESP32
#include "gamepad.h"
#else
enum {
  GAMEPAD_INPUT_START = 0,
  GAMEPAD_INPUT_SELECT,
  GAMEPAD_INPUT_UP,
  GAMEPAD_INPUT_DOWN,
  GAMEPAD_INPUT_LEFT,
  GAMEPAD_INPUT_RIGHT,
  GAMEPAD_INPUT_A,
  GAMEPAD_INPUT_B,
  GAMEPAD_INPUT_MENU,
  GAMEPAD_INPUT_L,
  GAMEPAD_INPUT_R,
  GAMEPAD_INPUT_MAX
};

typedef struct {
  uint8_t values[GAMEPAD_INPUT_MAX];
} input_gamepad_state;
#endif

void hal_input_init(void);
void hal_input_read(input_gamepad_state *out);
void hal_input_poll(void);
