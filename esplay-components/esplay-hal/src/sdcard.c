#include "sdcard.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "sdcard";
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

static bool isOpen = false;
/** True while sd_mount task is running OR until it clears this flag (always cleared when task exits). */
static bool mount_async_started;

bool sdcard_is_mounted(void)
{
    return isOpen;
}

bool sdcard_mount_busy(void)
{
    return mount_async_started;
}

static char s_async_mount_path[16];

static void sdcard_mount_async_task(void *arg)
{
    (void)arg;
    esp_err_t ret = ESP_FAIL;

    /* Brief delay: power/domains stable after LCD/audio init (helps some cards on IDF 4.x). */
    vTaskDelay(pdMS_TO_TICKS(120));

    for (int attempt = 1; attempt <= 8; attempt++)
    {
        printf("sdcard: mount attempt %d/8 \"%s\"\n", attempt, s_async_mount_path);
        ESP_LOGI(TAG, "mount attempt %d/8", attempt);
        ret = sdcard_open(s_async_mount_path);
        if (ret == ESP_OK)
        {
            printf("sdcard: mounted OK\n");
            break;
        }
        printf("sdcard: mount error %s (0x%x)\n", esp_err_to_name(ret), (unsigned)ret);
        if (ret == ESP_ERR_INVALID_STATE)
            break;
        if (attempt < 8)
            vTaskDelay(pdMS_TO_TICKS(400));
    }

    ESP_LOGI(TAG, "async mount finished %s (0x%x)", esp_err_to_name(ret), (unsigned)ret);
    /*
     * CRITICAL: must clear so future boots / logic do not think a task is still running.
     * Previously this stayed true after a failed mount → UI showed SD not mounted forever.
     */
    mount_async_started = false;
    vTaskDelete(NULL);
}

void sdcard_start_mount_async(const char *base_path)
{
    if (!base_path)
    {
        ESP_LOGW(TAG, "start_mount_async: null base_path");
        return;
    }
    if (isOpen)
    {
        ESP_LOGI(TAG, "start_mount_async: skip, already mounted");
        return;
    }
    if (mount_async_started)
    {
        ESP_LOGI(TAG, "start_mount_async: skip, mount task already running");
        return;
    }
    size_t len = strlen(base_path);
    if (len == 0 || len >= sizeof(s_async_mount_path))
    {
        ESP_LOGW(TAG, "start_mount_async: bad path length %u", (unsigned)len);
        return;
    }
    memcpy(s_async_mount_path, base_path, len + 1);
    mount_async_started = true;
    ESP_LOGI(TAG, "start_mount_async: scheduling mount for \"%s\" on CPU1", s_async_mount_path);
    /*
     * Run on CPU1 so app_main (CPU0) can reach ui_init immediately.
     * Without a card, esp_vfs_fat_sdmmc_mount can block CPU1 for seconds while
     * polling ACMD41 (many retries); TWDT must not watch IDLE1 or we reboot.
     */
    BaseType_t ok = xTaskCreatePinnedToCore(
        sdcard_mount_async_task,
        "sd_mount",
        8192,
        NULL,
        3,
        NULL,
        1);
    if (ok != pdPASS)
    {
        mount_async_started = false;
        ESP_LOGE(TAG, "start_mount_async: xTaskCreatePinnedToCore failed");
    }
}

inline static void swap(char **a, char **b)
{
    char *t = *a;
    *a = *b;
    *b = t;
}

static int strcicmp(char const *a, char const *b)
{
    for (;; a++, b++)
    {
        int d = tolower((int)*a) - tolower((int)*b);
        if (d != 0 || !*a)
            return d;
    }
}

static int partition(char *arr[], int low, int high)
{
    char *pivot = arr[high];
    int i = (low - 1);

    for (int j = low; j <= high - 1; j++)
    {
        if (strcicmp(arr[j], pivot) < 0)
        {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

static void quick_sort(char *arr[], int low, int high)
{
    if (low < high)
    {
        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

static void sort_files(char **files, int count)
{
    if (count > 1)
    {
        quick_sort(files, 0, count - 1);
    }
}

int sdcard_get_files_count(const char *path)
{
    int file_count = 0;
    DIR *dirp;
    struct dirent *entry;

    if (!sdcard_is_mounted())
        return 0;

    dirp = opendir(path);
    if (dirp == NULL)
    {
        ESP_LOGD(TAG, "opendir \"%s\" failed: %s", path, strerror(errno));
        return 0;
    }
    while ((entry = readdir(dirp)) != NULL)
    {
        if (entry->d_type == DT_REG)
        { /* If the entry is a regular file */
            file_count++;
        }
    }
    closedir(dirp);

    return file_count;
}

int sdcard_files_get(const char *path, const char *extension, char ***filesOut)
{
    const int MAX_FILES = 2048;

    int count = 0;
    char **result = (char **)malloc(MAX_FILES * sizeof(void *));
    if (!result)
        abort();

    if (!sdcard_is_mounted())
        return 0;

    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        ESP_LOGD(TAG, "opendir \"%s\" failed: %s", path, strerror(errno));
        return 0;
    }

    int extensionLength = strlen(extension);
    if (extensionLength < 1)
        abort();

    char *temp = (char *)malloc(extensionLength + 1);
    if (!temp)
        abort();

    memset(temp, 0, extensionLength + 1);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // printf("Ŀ¼dir:%s %d ",entry->d_name,strlen(entry->d_name));
        // for (size_t i = 0; i < 20; i++)
        // {
        //     printf("%x ",entry->d_name[i]);
        // }
        // printf("\n");

        size_t len = strlen(entry->d_name);

        // ignore 'hidden' files (MAC)
        bool skip = false;
        if (entry->d_name[0] == '.')
            skip = true;

        memset(temp, 0, extensionLength + 1);
        if (!skip)
        {
            for (int i = 0; i < extensionLength; ++i)
            {
                temp[i] = tolower((int)entry->d_name[len - extensionLength + i]);
            }

            if (len > extensionLength)
            {
                if (strcmp(temp, extension) == 0)
                {
                    result[count] = (char *)malloc(len + 1);
                    // printf("%s: allocated %p\n", __func__, result[count]);

                    if (!result[count])
                    {
                        abort();
                    }

                    strcpy(result[count], entry->d_name);
                    ++count;

                    if (count >= MAX_FILES)
                        break;
                }
            }
        }
    }

    closedir(dir);
    free(temp);

    sort_files(result, count);

    *filesOut = result;
    return count;
}

void sdcard_files_free(char **files, int count)
{
    for (int i = 0; i < count; ++i)
    {
        // printf("%s: freeing item %p\n", __func__, files[i]);
        free(files[i]);
    }

    // printf("%s: freeing array %p\n", __func__, files);
    free(files);
}

esp_err_t sdcard_open(const char *base_path)
{
    esp_err_t ret;

    if (isOpen)
    {
        ESP_LOGW(TAG, "sdcard_open(\"%s\"): already open", base_path ? base_path : "");
        ret = ESP_FAIL;
    }
    else
    {
        ESP_LOGI(TAG, "sdcard_open(\"%s\"): mounting (SDMMC 1-bit)...", base_path ? base_path : "");
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.flags = SDMMC_HOST_FLAG_1BIT;
        /*
         * IDF 3.x stacks often ran effectively slower buses; 40MHz HS can fail marginally on 4.x.
         * ESPlay Micro: SDMMC 1-line — default 20MHz is OK for mount; concurrent MP3 decode + I2S
         * can still hit CRC/timeouts on marginal cards/rails — 10MHz is a stable playback default.
         */
        host.max_freq_khz = 10000;
        host.command_timeout_ms = 1500;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1;
#if SOC_SDMMC_USE_GPIO_MATRIX
        /*
         * SDMMC_SLOT_CONFIG_DEFAULT assigns DAT1/DAT2/DAT3 to GPIO 4/12/13.
         * ESPlay uses GPIO 4 (AMP_SHDN), 12 (LCD DC), 13 (LED) — must not be SD pins in 1-bit mode.
         */
        slot_config.d1 = GPIO_NUM_NC;
        slot_config.d2 = GPIO_NUM_NC;
        slot_config.d3 = GPIO_NUM_NC;
#endif
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        /* In 1-bit mode only CMD and D0 are part of the active bus. Do not touch
         * D1/D2 as GPIOs: on ESPlay they are shared with amp and LCD DC.
         * DAT3 still must stay high for SD mode; IDF v4 only pulls it in 4-bit mode. */
        gpio_pullup_en(GPIO_NUM_15); /* CMD */
        gpio_pullup_en(GPIO_NUM_2);  /* D0  */
        gpio_pullup_en(GPIO_NUM_13); /* D3 / CD, keep high */

        // Options for mounting the filesystem.
        // If format_if_mount_failed is set to true, SD card will be partitioned and
        // formatted in case when mounting fails.
        esp_vfs_fat_sdmmc_mount_config_t mount_config;
        memset(&mount_config, 0, sizeof(mount_config));

        mount_config.format_if_mount_failed = false;
        mount_config.max_files = 10;
        mount_config.allocation_unit_size = 0;

        sdmmc_card_t *card = NULL;
        ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &card);

        if (ret == ESP_OK)
        {
            isOpen = true;
            printf("sdcard: FAT ok at \"%s\", ~%llu KiB\n",
                   base_path,
                   (unsigned long long)(((uint64_t)card->csd.capacity) * card->csd.sector_size / 1024));
            ESP_LOGI(TAG, "mounted FAT at \"%s\", capacity ~%llu KiB",
                     base_path,
                     (unsigned long long)(((uint64_t)card->csd.capacity) * card->csd.sector_size / 1024));
        }
        else
        {
            printf("sdcard: esp_vfs_fat_sdmmc_mount failed %s (0x%x)\n", esp_err_to_name(ret), (unsigned)ret);
            ESP_LOGW(TAG, "mount failed: %s (0x%x)", esp_err_to_name(ret), (unsigned)ret);
        }
    }

    return ret;
}

esp_err_t sdcard_close()
{
    esp_err_t ret;

    if (!isOpen)
    {
        printf("sdcard_close: not open.\n");
        ret = ESP_FAIL;
    }
    else
    {
        ret = esp_vfs_fat_sdmmc_unmount();

        if (ret != ESP_OK)
        {
            printf("sdcard_close: esp_vfs_fat_sdmmc_unmount failed (%d)\n", ret);
        }
        else
        {
            isOpen = false;
        }
    }

    return ret;
}

size_t sdcard_get_filesize(const char *path)
{
    size_t ret = 0;

    if (!isOpen)
    {
        printf("sdcard_get_filesize: not open.\n");
    }
    else
    {
        FILE *f = fopen(path, "rb");
        if (f == NULL)
        {
            printf("sdcard_get_filesize: fopen failed.\n");
        }
        else
        {
            // get the file size
            fseek(f, 0, SEEK_END);
            ret = ftell(f);
            fseek(f, 0, SEEK_SET);
        }
    }

    return ret;
}

size_t sdcard_copy_file_to_memory(const char *path, void *ptr)
{
    size_t ret = 0;

    if (!isOpen)
    {
        printf("sdcard_copy_file_to_memory: not open.\n");
    }
    else
    {
        if (!ptr)
        {
            printf("sdcard_copy_file_to_memory: ptr is null.\n");
        }
        else
        {
            FILE *f = fopen(path, "rb");
            if (f == NULL)
            {
                printf("sdcard_copy_file_to_memory: fopen failed.\n");
            }
            else
            {
                // copy
                const size_t BLOCK_SIZE = 512;
                while (true)
                {
                    __asm__("memw");
                    size_t count = fread((uint8_t *)ptr + ret, 1, BLOCK_SIZE, f);
                    __asm__("memw");

                    ret += count;

                    if (count < BLOCK_SIZE)
                        break;
                }
            }
        }
    }

    return ret;
}

char *sdcard_create_savefile_path(const char *base_path, const char *fileName)
{
    char *result = NULL;

    if (!base_path)
        abort();
    if (!fileName)
        abort();

    // printf("%s: base_path='%s', fileName='%s'\n", __func__, base_path, fileName);

    // Determine folder
    const char *extension = fileName + strlen(fileName); // place at NULL terminator
    while (extension != fileName)
    {
        if (*extension == '.')
        {
            ++extension;
            break;
        }
        --extension;
    }

    if (extension == fileName)
    {
        printf("%s: File extention not found.\n", __func__);
        abort();
    }

    // printf("%s: extension='%s'\n", __func__, extension);

    const char *DATA_PATH = "/esplay/data/";
    const char *SAVE_EXTENSION = ".sav";

    size_t savePathLength = strlen(base_path) + strlen(DATA_PATH) + strlen(extension) + 1 + strlen(fileName) + strlen(SAVE_EXTENSION) + 1;
    char *savePath = malloc(savePathLength);
    if (savePath)
    {
        strcpy(savePath, base_path);
        strcat(savePath, DATA_PATH);
        strcat(savePath, extension);
        strcat(savePath, "/");
        strcat(savePath, fileName);
        strcat(savePath, SAVE_EXTENSION);

        printf("%s: savefile_path='%s'\n", __func__, savePath);

        result = savePath;
    }

    return result;
}
