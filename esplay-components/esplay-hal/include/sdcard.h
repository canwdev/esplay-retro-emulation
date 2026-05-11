#pragma once

#include <stdbool.h>
#include "esp_err.h"

int sdcard_files_get(const char *path, const char *extension, char ***filesOut);
void sdcard_files_free(char **files, int count);
esp_err_t sdcard_open(const char *base_path);
/** Non-blocking: mounts on CPU1; UI may proceed without waiting */
void sdcard_start_mount_async(const char *base_path);
esp_err_t sdcard_close(void);
bool sdcard_is_mounted(void);
/** True while async mount task is running (success path clears quickly after mount). */
bool sdcard_mount_busy(void);
int sdcard_get_files_count(const char *path);
size_t sdcard_get_filesize(const char *path);
size_t sdcard_copy_file_to_memory(const char *path, void *ptr);
char *sdcard_create_savefile_path(const char *base_path, const char *fileName);