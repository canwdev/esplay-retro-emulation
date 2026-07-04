#include "preview_nes.h"

#include "appfs.h"
#include "file_manager.h"
#include "hal_audio.h"
#include "platform_log.h"
#include "platform_time.h"
#include "ui_app.h"

#include "nofrendo.h"
#include "nes/nes.h"
#include "nes/rom.h"

#include "preview_nes_platform.h"

#include <string.h>

static const char *TAG = "preview-nes";
static char s_current_path[512];

static bool can_open(const char *path) {
  if (!path) return false;
  const char *ext = strrchr(path, '.');
  return ext && strcasecmp(ext, ".nes") == 0;
}

static bool open_preview(const char *path, preview_open_args_t *args) {
  (void)args;
  strlcpy(s_current_path, path, sizeof(s_current_path));
  platform_log(PLATFORM_LOG_INFO, TAG, "opening: %s", path);

  hal_audio_stop();
  platform_sleep_ms(100);

  size_t rom_size = 0;
  uint8_t *rom_data = (uint8_t *)appfs_load_file(path, &rom_size);
  if (!rom_data) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "appfs_load_file failed: %s", path);
    hal_audio_init();
    return false;
  }
  platform_log(PLATFORM_LOG_INFO, TAG, "ROM via AppFS: %u bytes",
               (unsigned)rom_size);

  nes_t *nes = nes_init(SYS_DETECT, 32000, true, NULL);
  if (!nes) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "nes_init failed");
    appfs_release();
    hal_audio_init();
    return false;
  }

  rom_t *cart = rom_loadmem(rom_data, rom_size);
  if (!cart) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "rom_loadmem failed");
    nes_shutdown();
    appfs_release();
    hal_audio_init();
    return false;
  }

  int ret = nes_insertcart(cart);
  if (ret < 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "nes_insertcart failed: %d", ret);
    nes_shutdown();
    appfs_release();
    hal_audio_init();
    return false;
  }

  nes_platform_init(path);
  nes_platform_game_loop();

  nes_platform_deinit();
  nes_shutdown();
  appfs_release();

  hal_audio_init();

  lv_obj_clean(g_ui.screen);
  fm_create();

  return true;
}

static void close_preview(void) {
}

static const char *current_path(void) {
  return s_current_path;
}

const preview_app_t preview_nes_app = {
    .id           = "nes",
    .can_open     = can_open,
    .open         = open_preview,
    .close        = close_preview,
    .on_key       = NULL,
    .on_timer     = NULL,
    .current_path = current_path,
};
