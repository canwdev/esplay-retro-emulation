#include "appfs.h"

#include "esp_partition.h"
#include "platform_log.h"
#include "spi_flash_mmap.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "appfs";

static const esp_partition_t *s_part = NULL;
static const void *s_mapped_ptr = NULL;
static spi_flash_mmap_handle_t s_mmap_handle = 0;

#define APPFS_CHUNK 1024

bool appfs_init(void) {
  s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY, "appfs");
  if (!s_part) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "Partition 'appfs' not found");
    return false;
  }
  platform_log(PLATFORM_LOG_INFO, TAG, "partition size=%lu",
               (unsigned long)s_part->size);
  return true;
}

void *appfs_load_file(const char *path, size_t *out_size) {
  appfs_release();

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "fopen failed: %s", path);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fsize <= 0 || (size_t)fsize > s_part->size) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "file size %ld exceeds partition",
                 fsize);
    fclose(fp);
    return NULL;
  }

  size_t erase_len = ((size_t)fsize + 0xFFFF) & ~0xFFFF;
  esp_err_t err = esp_partition_erase_range(s_part, 0, erase_len);
  if (err != ESP_OK) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "erase failed: %d", err);
    fclose(fp);
    return NULL;
  }

  static uint8_t buf[APPFS_CHUNK];
  size_t off = 0;
  while (off < (size_t)fsize) {
    size_t chunk = (size_t)fsize - off;
    if (chunk > APPFS_CHUNK)
      chunk = APPFS_CHUNK;

    size_t rd = fread(buf, 1, chunk, fp);
    if (rd == 0)
      break;

    err = esp_partition_write_raw(s_part, off, buf, rd);
    if (err != ESP_OK) {
      platform_log(PLATFORM_LOG_ERROR, TAG, "write at %u failed: %d",
                   (unsigned)off, err);
      fclose(fp);
      return NULL;
    }
    off += rd;
  }
  fclose(fp);

  err = esp_partition_mmap(s_part, 0, (size_t)fsize, SPI_FLASH_MMAP_DATA,
                           &s_mapped_ptr, &s_mmap_handle);
  if (err != ESP_OK) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "mmap failed: %d", err);
    return NULL;
  }

  *out_size = (size_t)fsize;
  platform_log(PLATFORM_LOG_INFO, TAG, "loaded %s (%u bytes)", path,
               (unsigned)off);
  return (void *)s_mapped_ptr;
}

void appfs_release(void) {
  if (s_mapped_ptr) {
    spi_flash_munmap(s_mmap_handle);
    s_mapped_ptr = NULL;
    s_mmap_handle = 0;
  }
}
