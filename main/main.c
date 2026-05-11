/*
 * ESPlay minimal firmware: SD via HAL async mount + µGUI file browser.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio.h"
#include "display.h"
#include "esplay-ui.h"
#include "file_browser.h"
#include "gamepad.h"
#include "power.h"
#include "sdcard.h"
#include "settings.h"
#include "ugui.h"

#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "app";

#define SD_MOUNT "/sd"

void app_main(void)
{
    esp_log_level_set("sdcard", ESP_LOG_INFO);

    settings_init();
    esplay_system_init();

    gamepad_init();

    display_prepare();
    display_init();

    int32_t brightness = 50;
    settings_load(SettingBacklight, &brightness);
    set_display_brightness((int)brightness);

    battery_level_init();
    battery_state bat_state;
    battery_level_read(&bat_state);
    ESP_LOGI(TAG, "battery: %d%% (~%d mV)", bat_state.percentage, bat_state.millivolts);
    if (bat_state.percentage == 0) {
        display_show_empty_battery();
        display_poweroff();
        system_sleep();
        abort();
    }

    /*
     * AMP_SHDN is shared with ESP32 SDMMC DAT1 on ESPlay. Set it up before
     * mounting SD so later playback only toggles the level and does not remux GPIO4.
     */
    audio_amp_init();

    sdcard_start_mount_async(SD_MOUNT);

    ui_init();
    UG_FontSelect(&FONT_8X12);

    for (int i = 0; i < 300; i++) {
        if (sdcard_is_mounted()) {
            break;
        }
        if (!sdcard_mount_busy()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!sdcard_is_mounted()) {
        ui_clear_screen();
        UG_SetForecolor(C_RED);
        UG_SetBackcolor(C_BLACK);
        UG_PutString(40, 110, "SD card not mounted");
        ui_flush();
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    file_browser_run(SD_MOUNT);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
