#include "hal_input.h"

#include <SDL.h>
#include <string.h>

static input_gamepad_state s_state;

void hal_input_init(void) {
  memset(&s_state, 0, sizeof(s_state));
}

static uint8_t key_down(const uint8_t *keys, SDL_Scancode sc) {
  return keys[sc] ? 1 : 0;
}

void hal_input_poll(void) {
  SDL_PumpEvents();
  const uint8_t *keys = SDL_GetKeyboardState(NULL);

  s_state.values[GAMEPAD_INPUT_UP] = key_down(keys, SDL_SCANCODE_UP);
  s_state.values[GAMEPAD_INPUT_DOWN] = key_down(keys, SDL_SCANCODE_DOWN);
  s_state.values[GAMEPAD_INPUT_LEFT] = key_down(keys, SDL_SCANCODE_LEFT);
  s_state.values[GAMEPAD_INPUT_RIGHT] = key_down(keys, SDL_SCANCODE_RIGHT);

  s_state.values[GAMEPAD_INPUT_A] =
      (key_down(keys, SDL_SCANCODE_Z) || key_down(keys, SDL_SCANCODE_RETURN));
  s_state.values[GAMEPAD_INPUT_B] =
      (key_down(keys, SDL_SCANCODE_X) || key_down(keys, SDL_SCANCODE_BACKSPACE));

  s_state.values[GAMEPAD_INPUT_MENU] = key_down(keys, SDL_SCANCODE_M);
  s_state.values[GAMEPAD_INPUT_START] = key_down(keys, SDL_SCANCODE_S);
  s_state.values[GAMEPAD_INPUT_SELECT] = key_down(keys, SDL_SCANCODE_ESCAPE);

  s_state.values[GAMEPAD_INPUT_L] = key_down(keys, SDL_SCANCODE_Q);
  s_state.values[GAMEPAD_INPUT_R] = key_down(keys, SDL_SCANCODE_W);
}

void hal_input_read(input_gamepad_state *out) {
  if (!out)
    return;
  *out = s_state;
}
