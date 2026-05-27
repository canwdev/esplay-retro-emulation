#include "hal_input.h"

void hal_input_init(void) {
  gamepad_init();
}

void hal_input_read(input_gamepad_state *out) {
  gamepad_read(out);
}

void hal_input_poll(void) {
}
