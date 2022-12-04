// ESP System
#include <stdio.h>
#include "limits.h" /* PATH_MAX */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"

// Wi-Fi
// #include "esp_wifi.h"
// #include "esp_http_server.h"
// #include "esp_event_loop.h"

// Basics
#include "settings.h"
#include "power.h"
#include "display.h"
#include "gamepad.h"
#include "audio.h"
#include "power.h"
#include "sdcard.h"
#include "ugui.h"
#include "esplay-ui.h"


// Apps
// #include "app_audio_player.h"
#include "app_file_browser.h"

battery_state bat_state;

int32_t wifi_en = 0;
int32_t volume = 20;
int32_t volume_step = 2;
int32_t bright = 50;
int32_t scaling = SCALE_FIT;
int32_t scale_alg = NEAREST_NEIGHBOR;

// esp_err_t start_file_server(const char *base_path);
// esp_err_t event_handler(void *ctx, system_event_t *event)
// {
//     return ESP_OK;
// }
// void es_init_wifi_ap()
// {
//     if (wifi_en)
//     {
//         tcpip_adapter_init();
//         ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
//         wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//         ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//         ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
//         ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
//         wifi_config_t ap_config = {
//             .ap = {
//                 .ssid = "esplay",
//                 .authmode = WIFI_AUTH_OPEN,
//                 .max_connection = 2,
//                 .beacon_interval = 200}};
//         uint8_t channel = 5;
//         ap_config.ap.channel = channel;
//         ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

//         ESP_ERROR_CHECK(esp_wifi_start());

//         /* Start the file server */
//         ESP_ERROR_CHECK(start_file_server("/sd"));

//         printf("\n Wi-Fi Ready, AP on channel %d\n", (int)channel);
//     }
// }

void es_load_settings()
{
    if (settings_load(SettingWifi, &wifi_en) != 0)
        settings_save(SettingWifi, (int32_t)wifi_en);

    if (settings_load(SettingAudioVolume, &volume) != 0)
        settings_save(SettingAudioVolume, (int32_t)volume);

    if (settings_load(SettingStep, &volume_step) != 0)
        settings_save(SettingStep, (int32_t)volume_step);

    if (settings_load(SettingBacklight, &bright) != 0)
        settings_save(SettingBacklight, (int32_t)bright);

    if (settings_load(SettingScaleMode, &scaling) != 0)
        settings_save(SettingScaleMode, (int32_t)scaling);

    if (settings_load(SettingAlg, &scale_alg) != 0)
        settings_save(SettingAlg, (int32_t)scale_alg);
}

void es_init_system()
{
    settings_init();
    esplay_system_init();

    audio_init(44100);
    audio_amp_disable();

    gamepad_init();

    // Display
    display_prepare();
    display_init();

    int brightness = 50;
    settings_load(SettingBacklight, &brightness);
    set_display_brightness(brightness);


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
    // es_init_wifi_ap();

    ui_init();
}

void enter_app();
int render_settings();

#define MENU_COUNT 3
char menu_names[MENU_COUNT][10] = {"Files", "Music", "Games"};

void draw_home_screen()
{
    ui_clear_screen();

    /* START Status Bar */
    UG_SetForecolor(FG_COLOR_1);
    UG_SetBackcolor(BG_COLOR);
    char *title = "ESPlay Mod";
    int draw_top = 5;
    int draw_left = 5;
    ui_draw_x_center_string(draw_top, title);

    // Draw Volume & Battery
    uint8_t _volume = volume;
    settings_load(SettingAudioVolume, (int32_t *)&_volume);
    char volStr[8];
    sprintf(volStr, "Vol:%i", _volume);
    UG_PutString(draw_left, draw_top, volStr);

    battery_level_read(&bat_state);
    char batStr[8];
    sprintf(batStr, "Bat:%i%% %imV", bat_state.percentage, bat_state.millivolts);
    UG_PutString((SCREEN_WIDTH - 80), draw_top, batStr);
    /* END Status Bar */

    /* START Help Buttons */
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
    /* END Help Buttons */

    if (wifi_en)
    {
        UG_SetForecolor(FG_COLOR_3);
        UG_SetBackcolor(BG_COLOR);
        title = "Wi-Fi AP on http://192.168.4.1";
        ui_draw_x_center_string(SCREEN_HEIGHT - 18, title);
    }
}

void render_home()
{
    // Faster development
    enter_app(0);

    draw_home_screen();

    int menuIndex = 0;
    int doRefresh = 1;
    int lastUpdate = 0;
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
            lastUpdate = 0;
            if (menuIndex > MENU_COUNT - 1)
                menuIndex = 0;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_LEFT] && joystick.values[GAMEPAD_INPUT_LEFT])
        {
            menuIndex--;
            lastUpdate = 0;
            if (menuIndex < 0)
                menuIndex = MENU_COUNT - 1;
        }

        if (lastUpdate != 1)
        {
            // Render Current Menu
            UG_SetForecolor(FG_COLOR_2);
            UG_SetBackcolor(BG_COLOR);
            char text[320];
            sprintf(text, "[%i] %s", menuIndex, menu_names[menuIndex]);
            UG_FillFrame(0, 90, 319, 102, BG_COLOR);
            ui_draw_x_center_string(90, text);

            lastUpdate = 1;
        }

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
            enter_app(menuIndex);

            draw_home_screen();
            lastUpdate = 0;
            doRefresh = 1;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_B] && joystick.values[GAMEPAD_INPUT_B])
        {
            printf("B Pressed\n");

            draw_home_screen();
            lastUpdate = 0;
            doRefresh = 1;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_MENU] && joystick.values[GAMEPAD_INPUT_MENU])
        {
            printf("Menu Pressed\n");

            int r = render_settings();
            if (r)
                esp_restart();

            draw_home_screen();
            lastUpdate = 0;
            doRefresh = 1;
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

void enter_app(int menuIndex)
{
    if (menuIndex == 0)
    {
        app_file_browser((FileBrowserParam){.cwd = "/sd"});
    }
    else if (menuIndex == 1)
    {
        // Entering Music...

        // #define AUDIO_FILE_PATH "/sd/audio"
        // Entry *new_entries;
        // int n_entries = fops_list_dir(&new_entries, AUDIO_FILE_PATH);
        // app_audio_player((AudioPlayerParam){new_entries, n_entries, 0, AUDIO_FILE_PATH, true});
        // fops_free_entries(&new_entries, n_entries);
    }
    else
    {
        printf("menuIndex %d is invalid!\n", menuIndex);
    }
}

#define SETTINGS_COUNT 6
char settings_names[SETTINGS_COUNT][20] = {"Wi-Fi AP", "Volume", "Step", "Brightness", "Upscaler", "Scale Alg"};
char scaling_text[3][20] = {"Native", "Normal", "Stretch"};
char scaling_alg_text[3][20] = {"Nearest Neighbor", "Bilinier Intrp.", "Box Filtered"};

void draw_settings(int index)
{
    ui_clear_screen();

    /* START Header */
    UG_FillFrame(0, 0, 320 - 1, 16 - 1, FG_COLOR_1);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(FG_COLOR_1);
    char *msg = "Settings";
    ui_draw_x_center_string(2, msg);
    /* END Help Buttons */

    /* START Footer */
    UG_FillFrame(0, 240 - 16 - 1, 320 - 1, 240 - 1, FG_COLOR_3);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_YELLOW_GREEN);
    msg = "U/D=Browse </>=Change B=Back";
    ui_draw_x_center_string(240 - 15, msg);
    /* END Footer */

    /* START System Info */
    esp_app_desc_t *desc = esp_ota_get_app_description();

    UG_SetForecolor(FG_COLOR_2);
    UG_SetBackcolor(BG_COLOR);
    char verStr[40];
    sprintf(verStr, "%s, IDF %s", desc->project_name, desc->idf_ver);
    UG_PutString(5, 240 - 72, verStr);
    UG_PutString(5, 240 - 58, desc->version);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    sprintf(verStr, "Silicon Rev%d, %dMB %s flash", chip_info.revision,
            spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    UG_PutString(5, 240 - 44, verStr);

    /* END System Info */

    int top = 26;
    for (int i = 0; i < SETTINGS_COUNT; i++)
    {
        if (i == index)
        {
            UG_FillFrame(0, top, SCREEN_WIDTH, top + 12, C_DARK_CYAN);
            UG_SetBackcolor(C_DARK_CYAN);
            UG_SetForecolor(C_WHITE);
        }
        else
        {
            UG_SetBackcolor(BG_COLOR);
            UG_SetForecolor(FG_COLOR_2);
        }

        UG_PutString(5, top, settings_names[i]);

        // show value on right side

        char str[3];
        switch (i)
        {
        case 0:
            ui_display_switch(307, top, wifi_en, C_WHITE, FG_COLOR_1, FG_COLOR_2);
            break;
        case 1:
            sprintf(str, "%d", volume);
            UG_PutString(SCREEN_WIDTH - 130, top, str);
            ui_display_seekbar((SCREEN_WIDTH - 103), top + 4, 100, (volume * 100) / 100, C_WHITE, FG_COLOR_1);
            break;
        case 2:
            sprintf(str, "%d", volume_step);
            ui_draw_y_right_string(top, str);

            break;
        case 3:
            sprintf(str, "%d", bright);
            UG_PutString(SCREEN_WIDTH - 130, top, str);
            ui_display_seekbar((SCREEN_WIDTH - 103), top + 4, 100, (bright * 100) / 100, C_WHITE, FG_COLOR_1);
            break;
        case 4:
            ui_draw_y_right_string(top, scaling_text[scaling]);

            break;
        case 5:
            ui_draw_y_right_string(top, scaling_alg_text[scale_alg]);

            break;
        default:
            break;
        }

        top += 17;
    }

    ui_flush();
}

int toggle_settings(int index, bool isRight)
{
    if (index == 0)
    {
        wifi_en = !wifi_en;
        settings_save(SettingWifi, (int32_t)wifi_en);
        return 1;
    }
    else if (index == 1)
    {
        if (isRight)
        {
            volume += volume_step;
            if (volume > 100)
                volume = 100;
        }
        else
        {
            volume -= volume_step;
            if (volume < 0)
                volume = 0;
        }
        settings_save(SettingAudioVolume, (int32_t)volume);
    }
    else if (index == 2)
    {
        if (isRight)
        {
            volume_step += 1;
            if (volume_step > 10)
                volume_step = 10;
        }
        else
        {
            volume_step -= 1;
            if (volume_step < 1)
                volume_step = 1;
        }
        settings_save(SettingStep, (int32_t)volume_step);
    }
    else if (index == 3)
    {
        if (isRight)
        {
            bright += volume_step;
            if (bright > 100)
                bright = 100;
        }
        else
        {
            bright -= volume_step;
            if (bright < 1)
                bright = 1;
        }
        set_display_brightness(bright);
        settings_save(SettingBacklight, (int32_t)bright);
    }
    else if (index == 4)
    {
        if (isRight)
        {
            scaling++;
            if (scaling > 2)
                scaling = 0;
        }
        else
        {
            scaling--;
            if (scaling < 0)
                scaling = 2;
        }
        settings_save(SettingScaleMode, (int32_t)scaling);
    }
    else if (index == 5)
    {
        if (isRight)
        {
            scale_alg++;
            if (scale_alg > 1)
                scale_alg = 0;
        }
        else
        {
            scale_alg--;
            if (scale_alg < 0)
                scale_alg = 1;
        }
        settings_save(SettingAlg, (int32_t)scale_alg);
    }

    draw_settings(index);
    return 0;
}

int render_settings()
{
    int32_t wifi_state = wifi_en;
    int selected = 0;
    draw_settings(selected);

    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (1)
    {
        input_gamepad_state key;
        gamepad_read(&key);

        if (!prevKey.values[GAMEPAD_INPUT_DOWN] && key.values[GAMEPAD_INPUT_DOWN])
        {
            ++selected;
            if (selected > SETTINGS_COUNT - 1)
                selected = 0;
            draw_settings(selected);
        }
        else if (!prevKey.values[GAMEPAD_INPUT_UP] && key.values[GAMEPAD_INPUT_UP])
        {
            --selected;
            if (selected < 0)
                selected = SETTINGS_COUNT - 1;
            draw_settings(selected);
        }
        else if (!prevKey.values[GAMEPAD_INPUT_LEFT] && key.values[GAMEPAD_INPUT_LEFT])
        {
            int r = toggle_settings(selected, false);
            if (r)
            {
                break;
            }
        }
        else if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT])
        {
            int r = toggle_settings(selected, true);
            if (r)
            {
                break;
            }
        }
        else if (!prevKey.values[GAMEPAD_INPUT_B] && key.values[GAMEPAD_INPUT_B])
        {
            break;
        }

        prevKey = key;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (wifi_en != wifi_state)
        return 1;

    return 0;
}
