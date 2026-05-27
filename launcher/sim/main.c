#include "hal_display.h"
#include "hal_input.h"
#include "input_bridge.h"
#include "platform_time.h"
#include "ui_app.h"
#include "ui_backlight.h"
#include "ui_font.h"
#include "ui_home.h"
#include "ui_theme.h"

#include "lvgl.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdbool.h>

ui_state_t g_ui = {0};

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  hal_display_flush(area->x1, area->y1, area->x2, area->y2, px_map);
  lv_display_flush_ready(disp);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    return 1;

  hal_display_init();
  hal_input_init();

  lv_init();
  ui_theme_init();

  lv_display_t *disp = lv_display_create(HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT);
  static lv_color_t fb[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
  lv_display_set_buffers(disp, fb, NULL, sizeof(fb),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, lvgl_flush_cb);
  lv_display_set_default(disp);
  ui_font_apply_display(disp);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(indev, input_bridge_lvgl_read);
  g_ui.input_device = indev;

  g_ui.screen = lv_obj_create(NULL);
  lv_screen_load(g_ui.screen);

  g_ui.input_group = lv_group_create();
  lv_group_set_default(g_ui.input_group);
  lv_indev_set_group(g_ui.input_device, g_ui.input_group);

  ui_backlight_init();
  input_bridge_init();
  ui_home_create();

  uint32_t last_ms = platform_millis();
  bool running = true;
  while (running) {
    hal_input_poll();
    input_bridge_poll();

    if (SDL_QuitRequested())
      running = false;

    uint32_t now_ms = platform_millis();
    uint32_t delta = now_ms - last_ms;
    if (delta > 0) {
      lv_tick_inc(delta);
      last_ms = now_ms;
    }

    uint32_t ms = lv_timer_handler();
    platform_sleep_ms(ms > 10 ? 10 : ms);
  }

  SDL_Quit();
  return 0;
}
