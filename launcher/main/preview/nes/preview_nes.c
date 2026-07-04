#include "preview_nes.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdcard.h"
#include <string.h>

static const char *TAG = "preview-nes";

static bool can_open(const char *path)
{
    if (!path)
        return false;
    const char *ext = strrchr(path, '.');
    return ext && strcasecmp(ext, ".nes") == 0;
}

static bool open_preview(const char *path, preview_open_args_t *args)
{
    (void)args;
    ESP_LOGI(TAG, "Launching NES via OTA: %s", path);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("boot", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %d", err);
        return false;
    }
    nvs_set_str(nvs, "boot_path", path);
    nvs_commit(nvs);
    nvs_close(nvs);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "nes_app");
    if (!part) {
        ESP_LOGE(TAG, "nes_app partition not found");
        return false;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d", err);
        return false;
    }

    sdcard_close();
    esp_restart();
    return true;
}

static void close_preview(void) {}

static const char *current_path(void) { return NULL; }

const preview_app_t preview_nes_app = {
    .id           = "nes",
    .can_open     = can_open,
    .open         = open_preview,
    .close        = close_preview,
    .on_key       = NULL,
    .on_timer     = NULL,
    .current_path = current_path,
};
