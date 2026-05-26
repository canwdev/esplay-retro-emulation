/* Esplay Launcher Main File */
#include "audio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gamepad.h"
#include "input_bridge.h"
#include "lcd.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "power.h"
#include "sdcard.h"
#include "settings.h"
#include "ui_app.h"
#include "ui_home.h"
#include "ui_chrome.h"
#include "ui_settings.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "ui_backlight.h"

static const char *TAG = "launcher";

ui_state_t g_ui = {0};

static esp_lcd_panel_handle_t panel_handle;

#define LVGL_TICK_PERIOD_MS 5

void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
  lcd_draw(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1,
           px_map);
  lv_display_flush_ready(disp);
}

static void increase_lvgl_tick(void *arg) {
  (void)arg;
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void init_system_components(void) {
  ESP_LOGI(TAG, "Initializing NVS flash");
  nvs_flash_init();

  settings_init();

  ESP_LOGI(TAG, "Initializing battery level");
  battery_level_init();

  ESP_LOGI(TAG, "Initializing LCD display");
  lcd_init(&panel_handle);

  ESP_LOGI(TAG, "Initializing gamepad");
  gamepad_init();

  audio_init();
}

static void init_lvgl_display(void) {
  ESP_LOGI(TAG, "Initializing LVGL library");
  lv_init();
  ui_theme_init();
  ui_settings_load_persisted();

  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

  static lv_color_t buf1[LCD_HEIGHT * LCD_WIDTH / 10];
  static lv_color_t buf2[LCD_HEIGHT * LCD_WIDTH / 10];
  lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_display_set_flush_cb(disp, lvgl_flush_cb);
  lv_display_set_default(disp);
  ui_font_apply_display(disp);

  lv_indev_t *indev = lv_indev_create();
  if (!indev) {
    ESP_LOGE(TAG, "Failed to create LVGL input device");
    return;
  }
  lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(indev, input_bridge_lvgl_read);
  g_ui.input_device = indev;

  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &increase_lvgl_tick, .name = "lvgl_tick"};
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(
      esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

  g_ui.screen = lv_obj_create(NULL);
  lv_screen_load(g_ui.screen);

  g_ui.input_group = lv_group_create();
  lv_group_set_default(g_ui.input_group);
  lv_indev_set_group(g_ui.input_device, g_ui.input_group);

  ui_backlight_init();
  input_bridge_init();
  ui_home_create();
}

static void init_ui(void) { sdcard_open("/sd"); }

static void run_main_loop(void) {
  TickType_t xLast = xTaskGetTickCount();
  while (1) {
    uint32_t time_till_next = lv_timer_handler();
    /* During audio playback throttle the LVGL render loop to ~25 fps so the
     * LCD SPI DMA and esp_timer overhead don't compete with the audio task. */
    uint32_t max_delay = audio_is_playing() ? 40 : 10;
    uint32_t delay = (time_till_next > max_delay) ? max_delay : time_till_next;
    vTaskDelay(pdMS_TO_TICKS(delay));

    TickType_t xNow = xTaskGetTickCount();
    if ((xNow - xLast) > pdMS_TO_TICKS(2000)) {
      ui_chrome_update_battery();
      xLast = xNow;
    }
  }
}

void app_main(void) {
  init_system_components();
  init_lvgl_display();
  init_ui();
  run_main_loop();
}
