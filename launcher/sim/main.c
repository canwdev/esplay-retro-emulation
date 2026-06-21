#include "hal_display.h"
#include "hal_audio.h"
#include "hal_input.h"
#include "hal_storage.h"
#include "input_bridge.h"
#include "preview_audio.h"
#include "platform_log.h"
#include "platform_time.h"
#include "ui_app.h"
#include "ui_backlight.h"
#include "ui_font.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_theme.h"

#include "lvgl.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdbool.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LONG WINAPI sim_unhandled_exception_filter(EXCEPTION_POINTERS *info) {
  DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
  platform_log(PLATFORM_LOG_ERROR, "crash", "Unhandled exception 0x%08lx", (unsigned long)code);

  void *frames[32] = {0};
  USHORT n = CaptureStackBackTrace(0, 32, frames, NULL);
  for (USHORT i = 0; i < n; i++) {
    platform_log(PLATFORM_LOG_ERROR, "crash", "bt[%u]=%p", (unsigned)i, frames[i]);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

ui_state_t g_ui = {0};

#if LV_USE_LOG
static void lvgl_log_cb(lv_log_level_t level, const char *buf) {
  int out = PLATFORM_LOG_INFO;
  if (level <= LV_LOG_LEVEL_ERROR)
    out = PLATFORM_LOG_ERROR;
  else if (level == LV_LOG_LEVEL_WARN)
    out = PLATFORM_LOG_WARN;
  else if (level == LV_LOG_LEVEL_INFO)
    out = PLATFORM_LOG_INFO;
  else
    out = PLATFORM_LOG_DEBUG;
  platform_log(out, "lvgl", "%s", buf ? buf : "");
}
#endif

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  hal_display_flush(area->x1, area->y1, area->x2, area->y2, px_map);
  lv_display_flush_ready(disp);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  platform_log(PLATFORM_LOG_INFO, "sim", "launcher_sim starting");

#ifdef _WIN32
  SetUnhandledExceptionFilter(sim_unhandled_exception_filter);
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
    platform_log(PLATFORM_LOG_ERROR, "sim", "SDL_Init failed: %s",
                 SDL_GetError());
    return 1;
  }

  hal_audio_init();
  hal_display_init();
  hal_input_init();
  if (!hal_storage_mount()) {
    platform_log(PLATFORM_LOG_WARN, "sim", "storage root not found: %s",
                 hal_storage_root());
  } else {
    platform_log(PLATFORM_LOG_INFO, "sim", "storage root: %s",
                 hal_storage_root());
  }

  lv_init();
#if LV_USE_LOG
  lv_log_register_print_cb(lvgl_log_cb);
#endif
  ui_theme_init();
  ui_settings_load_persisted();

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
  preview_audio_restore_persisted_session();
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
    uint32_t max_delay = ui_backlight_is_on() ? 10 : 50;
    if (ms > max_delay)
      ms = max_delay;
    platform_sleep_ms(ms);
  }

  SDL_Quit();
  return 0;
}
