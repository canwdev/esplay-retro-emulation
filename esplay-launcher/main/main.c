#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "settings.h"
#include "power.h"
#include "display.h"
#include "gamepad.h"
#include "audio.h"
#include "power.h"
#include "sdcard.h"
#include "ugui.h"
#include "esplay-ui.h"

battery_state bat_state;

int32_t volume = 20;
int32_t volume_step = 2;
int32_t bright = 50;

void es_load_settings()
{

    if (settings_load(SettingAudioVolume, &volume) != 0)
        settings_save(SettingAudioVolume, (int32_t)volume);

    if (settings_load(SettingStep, &volume_step) != 0)
        settings_save(SettingStep, (int32_t)volume_step);

    if (settings_load(SettingBacklight, &bright) != 0)
        settings_save(SettingBacklight, (int32_t)bright);
}

void es_init_system()
{
    settings_init();
    esplay_system_init();

    // Display
    display_prepare();
    display_init();
    int brightness = 50;
    settings_load(SettingBacklight, &brightness);
    set_display_brightness(brightness);

    gamepad_init();

    audio_init(44100);
    audio_amp_disable();

    battery_level_init();
    battery_level_read(&bat_state);
    if (bat_state.percentage == 0)
    {
        display_show_empty_battery();

        printf("PowerDown: Powerdown LCD panel.\n");
        display_poweroff();

        printf("PowerDown: Entering deep sleep.\n");
        system_sleep();

        // Should never reach here
        abort();
    }

    sdcard_open("/sd"); // map SD card.

    es_load_settings();

    ui_init();
}

void draw_home_screen()
{
    ui_clear_screen();
    UG_SetForecolor(C_ORANGE);
    UG_SetBackcolor(C_BLACK);
    char *title = "ESPlay Mod";
    int draw_top = 5;
    int draw_left = 5;
    UG_PutString((SCREEN_WIDTH / 2) - (strlen(title) * 9 / 2), draw_top, title);

    uint8_t _volume = volume;
    settings_load(SettingAudioVolume, (int32_t *)&_volume);
    char volStr[8];
    sprintf(volStr, "Vol:%i", _volume);
    UG_PutString(draw_left, draw_top, volStr);

    battery_level_read(&bat_state);
    char batStr[8];
    sprintf(batStr, "Bat:%i%% (%i)", bat_state.percentage, bat_state.millivolts);
    UG_PutString((SCREEN_WIDTH - 80), draw_top, batStr);
}

void render_home()
{
    draw_home_screen();
    ui_flush();
}

void app_main()
{
    printf("Hello world!\n");
    es_init_system();

    render_home();

    while (1)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
