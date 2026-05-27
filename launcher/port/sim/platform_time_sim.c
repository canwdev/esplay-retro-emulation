#include "platform_time.h"

#include <SDL.h>

uint32_t platform_millis(void) {
  return (uint32_t)SDL_GetTicks();
}

void platform_sleep_ms(uint32_t ms) {
  SDL_Delay(ms);
}
