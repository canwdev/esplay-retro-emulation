#include "preview_nes.h"

#include "appfs.h"
#include "file_manager.h"
#include "hal_audio.h"
#include "hal_display.h"
#include "platform_log.h"
#include "platform_time.h"
#include "preview_nes_display.h"
#include "preview_nes_platform.h"
#include "nofrendo.h"
#include "nesstate.h"
#include "ui_app.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "preview-nes";
static char s_current_path[512];

static bool preview_nes_can_open(const char *path) {
  if (!path) return false;
  const char *ext = strrchr(path, '.');
  if (!ext) return false;
  return strcasecmp(ext, ".nes") == 0;
}

static bool preview_nes_open(const char *path, preview_open_args_t *args) {
  (void)args;

  strlcpy(s_current_path, path, sizeof(s_current_path));
  platform_log(PLATFORM_LOG_INFO, TAG, "opening: %s", path);

  hal_audio_stop();
  platform_sleep_ms(100);

  size_t rom_size = 0;
  uint8_t *rom = (uint8_t *)appfs_load_file(path, &rom_size);
  if (!rom) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "appfs_load_file failed: %s", path);
    return false;
  }

  nes_platform_set_rom_data(rom);
  platform_log(PLATFORM_LOG_INFO, TAG, "ROM loaded via AppFS: %u bytes",
               (unsigned)rom_size);

  nes_platform_init(path);
  nes_display_write(NULL, 0);

  /* set save path */
  nes_platform_ensure_save_dir();
  {
    char sp[512];
    nes_platform_save_path(path, sp, sizeof(sp));
    nes_set_save_path(sp);
    platform_log(PLATFORM_LOG_INFO, TAG, "save path: %s", sp);
  }

  char *argv[2];
  argv[0] = (char *)"nes";
  argv[1] = (char *)path;
  nofrendo_main(2, argv);

  platform_log(PLATFORM_LOG_INFO, TAG, "nes exited, cleaning up...");

  nes_platform_deinit();
  appfs_release();
  nes_platform_set_rom_data(NULL);

  hal_audio_init();

  lv_obj_clean(g_ui.screen);
  fm_create();

  return true;
}

static void preview_nes_close(void) {
  /* Called when navigating away, but NES is self-contained */
}

static const char *preview_nes_current_path(void) {
  return s_current_path;
}

const preview_app_t preview_nes_app = {
    .id           = "nes",
    .can_open     = preview_nes_can_open,
    .open         = preview_nes_open,
    .close        = preview_nes_close,
    .on_key       = NULL,
    .on_timer     = NULL,
    .current_path = preview_nes_current_path,
};
