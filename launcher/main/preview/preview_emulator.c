#include "preview_emulator.h"

#include "file_manager.h"
#include "hal_storage.h"
#include "platform_log.h"
#include "platform_mem.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "preview_emu";

typedef struct {
    const char *extensions;
    const char *emu_name;
    int ota_slot;
} emu_entry_t;

static const emu_entry_t s_emu_map[] = {
    {"nes fc fds nsf", "nes",  1},
    {"smc sfc",        "snes", 1},
    {"gb",             "gb",   1},
    {"gbc",            "gbc",  1},
    {"gw",             "gw",   1},
    {"sms sg",         "sms",  1},
    {"gg",             "gg",   1},
    {"pce",            "pce",  1},
    {"lnx",            "lnx",  1},
    {"col rom",        "col",  1},
    // .smd (Super Magic Drive) is the standard Genesis dump format;
    // .md excluded — clashes with Markdown, which is handled by text preview.
    {"gen smd",        "md",   3},
    {"wad",            "doom", 2},
    {"mx1 mx2 dsk",    "msx",  4},
};

static bool s_active;

static const char *get_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

static bool match_ext(const char *ext_list, const char *ext)
{
    const char *p = ext_list;
    size_t ext_len = strlen(ext);

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *end = strchr(p, ' ');
        if (!end) end = p + strlen(p);
        size_t len = (size_t)(end - p);
        if (len == ext_len && strncasecmp(p, ext, len) == 0)
            return true;
        p = end;
    }
    return false;
}

static bool preview_emu_can_open(const char *path)
{
    const char *ext = get_ext(path);
    if (!ext || !*ext) return false;

    for (size_t i = 0; i < sizeof(s_emu_map) / sizeof(s_emu_map[0]); i++) {
        if (match_ext(s_emu_map[i].extensions, ext))
            return true;
    }
    return false;
}

static bool preview_emu_open(const char *path, preview_open_args_t *args)
{
    (void)args;

    const char *ext = get_ext(path);
    const char *emu_name = NULL;
    int ota_slot = -1;

    for (size_t i = 0; i < sizeof(s_emu_map) / sizeof(s_emu_map[0]); i++) {
        if (match_ext(s_emu_map[i].extensions, ext)) {
            emu_name = s_emu_map[i].emu_name;
            ota_slot = s_emu_map[i].ota_slot;
            break;
        }
    }

    if (!emu_name || ota_slot < 0) return false;

    const char *root = hal_storage_root();
    char config_path[FM_PATH_MAX];
    snprintf(config_path, sizeof(config_path),
             "%s/retro-go/config/boot.json", root);

    char config_dir[FM_PATH_MAX];
    snprintf(config_dir, sizeof(config_dir), "%s/retro-go/config", root);

    char mkdir_buf[FM_PATH_MAX];
    strncpy(mkdir_buf, config_dir, sizeof(mkdir_buf) - 1);
    mkdir_buf[sizeof(mkdir_buf) - 1] = '\0';
    for (char *p = mkdir_buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(mkdir_buf, 0755);
            *p = '/';
        }
    }
    mkdir(config_dir, 0755);

    FILE *f = fopen(config_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to write %s", config_path);
        return false;
    }

    fprintf(f, "{\"BootName\":\"%s\",\"BootArgs\":\"%s\",\"BootFlags\":0}\n",
            emu_name, path);
    fclose(f);

    ESP_LOGI(TAG, "Launching %s -> %s (OTA slot %d)", path, emu_name, ota_slot);

    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_MIN + ota_slot, NULL);
    if (partition) {
        esp_err_t err = esp_ota_set_boot_partition(partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA set boot failed: %s", esp_err_to_name(err));
            remove(config_path);
            return false;
        }
    } else {
        ESP_LOGE(TAG, "OTA slot %d partition not found", ota_slot);
        remove(config_path);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

    return true;
}

static void preview_emu_close(void)
{
    s_active = false;
}

static bool preview_emu_on_key(const input_gamepad_state *gp, const bool edge[])
{
    (void)gp;
    (void)edge;
    return false;
}

static void preview_emu_on_timer(void) {}

static const char *preview_emu_current_path(void)
{
    return NULL;
}

const preview_app_t preview_emulator_app = {
    .id = "emulator",
    .can_open = preview_emu_can_open,
    .open = preview_emu_open,
    .close = preview_emu_close,
    .on_key = preview_emu_on_key,
    .on_timer = preview_emu_on_timer,
    .current_path = preview_emu_current_path,
};
