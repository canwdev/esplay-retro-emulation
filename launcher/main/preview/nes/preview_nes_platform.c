#include "preview_nes_platform.h"

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "hal_input.h"
#include "hal_settings.h"
#include "platform_mem.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "nofrendo.h"
#include "nes/nes.h"
#include "nes/input.h"
#include "nes/ppu.h"
#include "nes/rom.h"
#include "nes/state.h"

#include "preview_nes_display.h"
#include "preview_nes_menu.h"

static const char *TAG = "nes-platform";

#define NES_AUDIO_RATE      32000
#define VIDBUF_SIZE         (NES_SCREEN_PITCH * NES_SCREEN_HEIGHT)
#define SAVE_DIR            "/sd/esplay/data/nes"

static char             s_rom_path[512];
static uint8_t         *s_vidbuf;
static i2s_chan_handle_t s_audio_i2s;
static bool             s_audio_i2s_enabled;
static int              s_audio_volume = 255;
static bool             s_quit;
static int              s_palette_idx = NES_PALETTE_PVM;
static int              s_scale_mode  = NES_SCALE_FILL;

const char *nes_platform_rom_path(void) { return s_rom_path; }

/* ----------------------------------------------------------- */
static void audio_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num  = 8;
  chan_cfg.dma_frame_num = 512;
  i2s_new_channel(&chan_cfg, &s_audio_i2s, NULL);

  i2s_std_config_t std_cfg = {
      .clk_cfg = {
          .sample_rate_hz = NES_AUDIO_RATE,
          .clk_src        = I2S_CLK_SRC_APLL,
          .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
      },
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)CONFIG_AUDIO_I2S_BCLK_GPIO,
          .ws   = (gpio_num_t)CONFIG_AUDIO_I2S_WS_GPIO,
          .dout = (gpio_num_t)CONFIG_AUDIO_I2S_DOUT_GPIO,
          .din  = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false, .bclk_inv = false, .ws_inv = false,
          },
      },
  };
  i2s_channel_init_std_mode(s_audio_i2s, &std_cfg);
  i2s_channel_enable(s_audio_i2s);
  s_audio_i2s_enabled = true;

  int32_t vol = 60;
  hal_settings_load(SettingAudioVolume, &vol);
  if (vol < 0) vol = 0;
  if (vol > 255) vol = 255;
  s_audio_volume = (int)vol;
}

static void audio_deinit(void) {
  if (s_audio_i2s_enabled) {
    i2s_channel_disable(s_audio_i2s);
    s_audio_i2s_enabled = false;
  }
  if (s_audio_i2s) {
    i2s_del_channel(s_audio_i2s);
    s_audio_i2s = NULL;
  }
}

/* ----------------------------------------------------------- */
static void blit_screen(uint8 *vidbuf) {
  if (vidbuf)
    nes_display_submit(vidbuf, NES_SCREEN_PITCH, s_scale_mode);
}

/* ----------------------------------------------------------- */
static uint32_t read_input(void) {
  input_gamepad_state st;
  hal_input_read(&st);
  uint32_t b = 0;
  if (!st.values[GAMEPAD_INPUT_A])      b |= NES_PAD_A;
  if (!st.values[GAMEPAD_INPUT_B])      b |= NES_PAD_B;
  if (!st.values[GAMEPAD_INPUT_SELECT]) b |= NES_PAD_SELECT;
  if (!st.values[GAMEPAD_INPUT_START])  b |= NES_PAD_START;
  if (!st.values[GAMEPAD_INPUT_UP])     b |= NES_PAD_UP;
  if (!st.values[GAMEPAD_INPUT_DOWN])   b |= NES_PAD_DOWN;
  if (!st.values[GAMEPAD_INPUT_LEFT])   b |= NES_PAD_LEFT;
  if (!st.values[GAMEPAD_INPUT_RIGHT])  b |= NES_PAD_RIGHT;
  return b;
}

static bool menu_triggered(void) {
  input_gamepad_state st;
  hal_input_read(&st);
  static bool prev;
  bool cur = !st.values[GAMEPAD_INPUT_MENU];
  bool edge = cur && !prev;
  prev = cur;
  return edge;
}

/* ----------------------------------------------------------- */
static void ensure_dir(void) {
  mkdir("/sd/esplay", 0755);
  mkdir("/sd/esplay/data", 0755);
  mkdir(SAVE_DIR, 0755);
}

static void save_path(const char *rom_path, char *out, size_t out_len) {
  const char *name = strrchr(rom_path, '/');
  if (!name) name = rom_path;
  else name++;
  snprintf(out, out_len, SAVE_DIR "/%s.sav", name);
}

static void sram_path(const char *rom_path, char *out, size_t out_len) {
  const char *name = strrchr(rom_path, '/');
  if (!name) name = rom_path;
  else name++;
  snprintf(out, out_len, SAVE_DIR "/%s.srm", name);
}

/* ----------------------------------------------------------- */
void nes_platform_init(const char *rom_path) {
  strlcpy(s_rom_path, rom_path, sizeof(s_rom_path));
  s_vidbuf = (uint8_t *)heap_caps_malloc(VIDBUF_SIZE, MALLOC_CAP_INTERNAL);
  if (!s_vidbuf)
    s_vidbuf = (uint8_t *)platform_malloc(VIDBUF_SIZE);
  if (!s_vidbuf) {
    ESP_LOGE(TAG, "Failed to allocate vidbuf (%d bytes)", VIDBUF_SIZE);
    return;
  }
  s_quit   = false;
  audio_init();
  nes_display_init();
  ensure_dir();
}

void nes_platform_deinit(void) {
  nes_display_deinit();
  audio_deinit();
  if (s_vidbuf) { free(s_vidbuf); s_vidbuf = NULL; }
}

/* ----------------------------------------------------------- */
void nes_platform_game_loop(void) {
  nes_t *nes = nes_getptr();

  uint16_t *pal = (uint16_t *)nofrendo_buildpalette(s_palette_idx, 16);
  if (!pal) return;
  nes_display_set_palette(pal);
  free(pal);

  nes->blit_func = blit_screen;
  ppu_setopt(PPU_LIMIT_SPRITES, 1);

  char spath[512];
  save_path(s_rom_path, spath, sizeof(spath));

  char srpath[512];
  sram_path(s_rom_path, srpath, sizeof(srpath));
  if (nes->cart->battery)
    rom_loadsram(srpath);

  int64_t fps_base  = 0;
  int     fps_count = 0;
  int64_t target_us = 1000000LL / nes->refresh_rate;
  int     skip      = 0;
  int64_t last_yield = 0;

  for (;;) {
    uint32_t buttons = read_input();
    input_update(0, buttons);

    if (menu_triggered()) {
      int r = nes_menu_show();
      switch (r) {
      case NES_MENU_SAVE:  state_save(spath);   break;
      case NES_MENU_LOAD:  state_load(spath);   break;
      case NES_MENU_RESET: nes_reset(true);     break;
      case NES_MENU_QUIT:  s_quit = true;       break;
      default: break;
      }
      if (s_quit) break;
    }

    int64_t t0     = esp_timer_get_time();
    bool drawFrame = !skip;

    if (drawFrame)
      nes_setvidbuf(s_vidbuf);

    nes_emulate(drawFrame);

    int64_t elapsed = esp_timer_get_time() - t0;

    int samples = nes->apu->samples_per_frame;
    short *abuf = nes->apu->buffer;

    if (abuf && samples > 0 && s_audio_i2s_enabled) {
      int vol = s_audio_volume;
      for (int i = 0; i < samples; i++) {
        int32_t s = (int32_t)abuf[i] * vol / 255;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        abuf[i] = (int16_t)s;
      }
      size_t nw = 0;
      i2s_channel_write(s_audio_i2s, (const uint8_t *)abuf,
                        samples * sizeof(int16_t), &nw, pdMS_TO_TICKS(50));
    }

    if (drawFrame && nes->cart->battery) {
      static int sram_counter;
      if (++sram_counter >= 60) {
        rom_savesram(srpath);
        sram_counter = 0;
      }
    }

    if (skip == 0) {
      if (elapsed > target_us + 1500)
        skip = 1;
    } else {
      skip--;
    }

    fps_count++;
    int64_t now = esp_timer_get_time();
    if (now - fps_base >= 2000000) {
      float fps = (float)fps_count * 1000000.0f / (float)(now - fps_base);
      ESP_LOGI(TAG, "FPS: %.1f  emu: %lld us  skip: %d",
               (double)fps, (long long)elapsed, skip);
      fps_base  = now;
      fps_count = 0;
    }

    if (now - last_yield > 4000000) {
      vTaskDelay(1);
      last_yield = now;
    }
  }
}


