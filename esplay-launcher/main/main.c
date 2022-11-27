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

#define MENU_COUNT 3
char menu_names[MENU_COUNT][10] = {"Files", "Music", "Test"};

#define BG_COLOR C_BLACK
#define FG_COLOR_1 C_ORANGE
#define FG_COLOR_2 C_CYAN
void draw_home_screen()
{
    ui_clear_screen();

    // Status Bar START
    UG_SetForecolor(FG_COLOR_1);
    UG_SetBackcolor(BG_COLOR);
    char *title = "ESPlay Mod";
    int draw_top = 5;
    int draw_left = 5;
    UG_PutString((SCREEN_WIDTH / 2) - (strlen(title) * 9 / 2), draw_top, title);

    // Draw Volume & Battery
    uint8_t _volume = volume;
    settings_load(SettingAudioVolume, (int32_t *)&_volume);
    char volStr[8];
    sprintf(volStr, "Vol:%i", _volume);
    UG_PutString(draw_left, draw_top, volStr);

    battery_level_read(&bat_state);
    char batStr[8];
    sprintf(batStr, "Bat:%i%% (%i)", bat_state.percentage, bat_state.millivolts);
    UG_PutString((SCREEN_WIDTH - 80), draw_top, batStr);
    // Status Bar END

    // Button Help START
    UG_SetForecolor(FG_COLOR_2);
    UG_SetBackcolor(BG_COLOR);
    const int _x1 = 40;
    const int _x2 = 160;
    const int _y1 = 50 + (56 * 2) + 13;
    UG_PutString(_x1, _y1, "    Browse");
    UG_PutString(_x1, _y1 + 18, "  Select");
    UG_PutString(_x2, _y1, "  Resume");
    UG_PutString(_x2, _y1 + 18, "     Settings");

    UG_SetForecolor(BG_COLOR);
    UG_SetBackcolor(FG_COLOR_2);
    UG_FillRoundFrame(35, _y1 - 1, 48 + (2 * 9) + 3, _y1 + 11, 7, FG_COLOR_2);
    UG_PutString(_x1, _y1, "< >");

    UG_FillCircle(43, _y1 + 18 + 5, 7, FG_COLOR_2);
    UG_PutString(_x1, _y1 + 18, "A");

    UG_FillCircle(_x2 + 3, _y1 + 5, 7, FG_COLOR_2);
    UG_PutString(_x2, _y1, "B");

    UG_FillRoundFrame(_x2 - 5, _y1 + 18 - 1, 168 + (3 * 9) + 3, _y1 + 18 + 11, 7, FG_COLOR_2);
    UG_PutString(_x2, _y1 + 18, "MENU");
    // Button Help END
}

void render_home()
{
    draw_home_screen();

    int menuIndex = 0;
    int doRefresh = 1;
    int oldArrowsTick = -1;
    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (1)
    {
        input_gamepad_state joystick;
        gamepad_read(&joystick);

        if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && joystick.values[GAMEPAD_INPUT_RIGHT])
        {
            menuIndex++;
            if (menuIndex > MENU_COUNT - 1)
                menuIndex = 0;
        }
        if (!prevKey.values[GAMEPAD_INPUT_LEFT] && joystick.values[GAMEPAD_INPUT_LEFT])
        {
            menuIndex--;
            if (menuIndex < 0)
                menuIndex = MENU_COUNT;
        }

        // Render Current Menu

        UG_SetForecolor(FG_COLOR_2);
        UG_SetBackcolor(BG_COLOR);
        char text[320];
        sprintf(text, "[%i] %s", menuIndex, menu_names[menuIndex]);
        UG_FillFrame(0, 90, 319, 102, BG_COLOR);
        UG_PutString((SCREEN_WIDTH / 2) - (strlen(text) * 9 / 2), 90, text);

        // Render arrows START
        int t = xTaskGetTickCount() / (400 / portTICK_PERIOD_MS);
        t = (t & 1);
        if (t != oldArrowsTick)
        {
            doRefresh = 1;

            if (t)
            {
                UG_SetForecolor(FG_COLOR_2);
                UG_SetBackcolor(BG_COLOR);
            }
            else
            {
                UG_SetForecolor(BG_COLOR);
                UG_SetBackcolor(FG_COLOR_2);
            }
            UG_PutString(10, 90, "<");
            UG_PutString((SCREEN_WIDTH - 20), 90, ">");

            oldArrowsTick = t;
        }
        // Render arrows END

        if (!prevKey.values[GAMEPAD_INPUT_A] && joystick.values[GAMEPAD_INPUT_A])
        {
            printf("A Pressed\n");
        }

        if (!prevKey.values[GAMEPAD_INPUT_B] && joystick.values[GAMEPAD_INPUT_B])
        {
            printf("B Pressed\n");
        }

        if (!prevKey.values[GAMEPAD_INPUT_MENU] && joystick.values[GAMEPAD_INPUT_MENU])
        {
            printf("Menu Pressed\n");
        }

        if (doRefresh)
        {
            ui_flush();
        }

        doRefresh = 0;
        prevKey = joystick;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
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
