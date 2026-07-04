#include "preview_nes_platform.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_input.h"
#include "hal_settings.h"
#include "platform_mem.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* --- nofrendo core headers --- */
#include "noftypes.h"
#include "bitmap.h"
#include "event.h"
#include "log.h"
#include "nes.h"
#include "nes_pal.h"
#include "nesinput.h"
#include "nesstate.h"
#include "nofrendo.h"
#include "osd.h"
#include "vid_drv.h"

/* restore stdbool for our own code after nofrendo defines its enum bool */
#include <stdbool.h>

#include "preview_nes_display.h"
#include "preview_nes_menu.h"

static const char *TAG = "nes-platform";

/* ------------------------------------------------------------------ */
/*  ROM data                                                          */
/* ------------------------------------------------------------------ */
static uint8_t *s_rom_data = NULL;

char *osd_getromdata(void) { return (char *)s_rom_data; }

uint8_t *nes_platform_get_rom_data(void) { return s_rom_data; }
void nes_platform_set_rom_data(uint8_t *p) { s_rom_data = p; }

/* Rom path for save states */
static char s_rom_path[512] = {0};

const char *nes_platform_rom_path(void) { return s_rom_path; }

/* ------------------------------------------------------------------ */
/*  Audio                                                             */
/* ------------------------------------------------------------------ */
#define NES_AUDIO_RATE 32000
#define NES_FRAG_SIZE   512

static void (*s_audio_callback)(void *buffer, int length) = NULL;
static int16_t *s_audio_buf = NULL;
static i2s_chan_handle_t s_audio_i2s = NULL;
static bool s_audio_i2s_enabled = false;
static int s_audio_volume = 255;

void nes_platform_audio_init(void) {
  s_audio_buf = (int16_t *)platform_malloc(NES_FRAG_SIZE * sizeof(int16_t) * 2);

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num  = 8;
  chan_cfg.dma_frame_num = 512;
  i2s_new_channel(&chan_cfg, &s_audio_i2s, NULL);

  i2s_std_config_t std_cfg = {
      .clk_cfg =
          {
              .sample_rate_hz = NES_AUDIO_RATE,
              .clk_src        = I2S_CLK_SRC_APLL,
              .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
          },
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)CONFIG_AUDIO_I2S_BCLK_GPIO,
              .ws   = (gpio_num_t)CONFIG_AUDIO_I2S_WS_GPIO,
              .dout = (gpio_num_t)CONFIG_AUDIO_I2S_DOUT_GPIO,
              .din  = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv   = false,
                  },
          },
  };
  i2s_channel_init_std_mode(s_audio_i2s, &std_cfg);
  i2s_channel_enable(s_audio_i2s);
  s_audio_i2s_enabled = true;

  s_audio_volume = 255;
  {
    int32_t vol = 60;
    hal_settings_load(SettingAudioVolume, &vol);
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    s_audio_volume = (int)vol;
  }

  ESP_LOGI(TAG, "NES audio I2S init, rate=%d", NES_AUDIO_RATE);
}

void nes_platform_audio_deinit(void) {
  if (s_audio_i2s_enabled) {
    i2s_channel_disable(s_audio_i2s);
    s_audio_i2s_enabled = false;
  }
  if (s_audio_i2s) {
    i2s_del_channel(s_audio_i2s);
    s_audio_i2s = NULL;
  }
  if (s_audio_buf) {
    platform_free(s_audio_buf);
    s_audio_buf = NULL;
  }
  s_audio_callback = NULL;
}

void nes_platform_audio_submit(const int16_t *samples, size_t count) {
  if (!s_audio_i2s || !s_audio_i2s_enabled)
    return;
  size_t nw = 0;
  i2s_channel_write(s_audio_i2s, (const uint8_t *)samples,
                    count * sizeof(int16_t), &nw, pdMS_TO_TICKS(50));
}

int nes_platform_audio_get_volume(void) { return s_audio_volume; }

void nes_platform_audio_set_volume(int vol) {
  if (vol < 0) vol = 0;
  if (vol > 255) vol = 255;
  s_audio_volume = vol;
}

/* ------------------------------------------------------------------ */
/*  Video                                                             */
/* ------------------------------------------------------------------ */
volatile int showOverlay = 0;

static uint8_t *s_lcdfb = NULL;

static void vid_custom_blit(bitmap_t *bmp, int n, rect_t *r) {
  (void)n; (void)r;
  if (!s_lcdfb)
    s_lcdfb = (uint8_t *)platform_malloc(NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT);
  if (bmp->line[0] && s_lcdfb) {
    memcpy(s_lcdfb, bmp->line[0], NES_SCREEN_WIDTH * NES_VISIBLE_HEIGHT);
    nes_display_write(s_lcdfb, NES_SCALE_FILL);
  }
}

/* ------------------------------------------------------------------ */
/*  Timer                                                             */
/* ------------------------------------------------------------------ */
static esp_timer_handle_t s_nes_timer = NULL;

int osd_installtimer(int frequency, void *func, int funcsize,
                     void *counter, int countersize) {
  (void)funcsize; (void)counter; (void)countersize;
  esp_timer_create_args_t targs = {
      .callback   = (esp_timer_cb_t)func,
      .name       = "nes_frame",
  };
  esp_timer_create(&targs, &s_nes_timer);
  esp_timer_start_periodic(s_nes_timer, 1000000ULL / frequency);
  return 0;
}

void nes_platform_timer_init(void) {
  /* Timer is installed by osd_installtimer() during nofrendo_main() */
}

void nes_platform_timer_deinit(void) {
  if (s_nes_timer) {
    esp_timer_stop(s_nes_timer);
    esp_timer_delete(s_nes_timer);
    s_nes_timer = NULL;
  }
}

/* ------------------------------------------------------------------ */
/*  Input                                                             */
/* ------------------------------------------------------------------ */
static input_gamepad_state s_prev_input;

/* Returns NES gamepad bits: bit13=A, bit14=B, bit0=Select, bit3=Start,
 * bit4=Up, bit5=Right, bit6=Down, bit7=Left */
static int nes_input_to_bits(void) {
  input_gamepad_state st;
  hal_input_read(&st);

  int result = 0;
  if (!st.values[GAMEPAD_INPUT_A])      result |= (1 << 13);
  if (!st.values[GAMEPAD_INPUT_B])      result |= (1 << 14);
  if (!st.values[GAMEPAD_INPUT_SELECT]) result |= (1 << 0);
  if (!st.values[GAMEPAD_INPUT_START])  result |= (1 << 3);
  if (!st.values[GAMEPAD_INPUT_RIGHT])  result |= (1 << 5);
  if (!st.values[GAMEPAD_INPUT_LEFT])   result |= (1 << 7);
  if (!st.values[GAMEPAD_INPUT_UP])     result |= (1 << 4);
  if (!st.values[GAMEPAD_INPUT_DOWN])   result |= (1 << 6);

  /* MENU key: edge detect (release) */
  if (s_prev_input.values[GAMEPAD_INPUT_MENU] &&
      !st.values[GAMEPAD_INPUT_MENU]) {
    showOverlay = 1;
  }

  s_prev_input = st;
  return result;
}

/* ------------------------------------------------------------------ */
/*  OSD Audio implementation                                           */
/* ------------------------------------------------------------------ */
void osd_setsound(void (*playfunc)(void *buffer, int length)) {
  s_audio_callback = playfunc;
}

void osd_getsoundinfo(sndinfo_t *info) {
  info->sample_rate = NES_AUDIO_RATE;
  info->bps = 16;
}

static void osd_stopsound(void) {
  s_audio_callback = NULL;
}

static int osd_init_sound(void) {
  s_audio_callback = NULL;
  return 0;
}

void do_audio_frame(void) {
  if (!s_audio_callback || !s_audio_buf)
    return;
  int left = NES_AUDIO_RATE / NES_REFRESH_RATE;
  while (left > 0) {
    int n = NES_FRAG_SIZE;
    if (n > left) n = left;
    s_audio_callback(s_audio_buf, n);

    int16_t *dst = s_audio_buf;
    int vol = s_audio_volume;
    for (int i = n - 1; i >= 0; i--) {
      int32_t s = (int32_t)s_audio_buf[i] * vol / 255;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      dst[i * 2]     = (int16_t)s;
      dst[i * 2 + 1] = (int16_t)s;
    }
    nes_platform_audio_submit(dst, n * 2);
    left -= n;
  }
}

/* ------------------------------------------------------------------ */
/*  OSD Video implementation                                           */
/* ------------------------------------------------------------------ */
/* NES palette: 256 entries, RGB indexed */
static uint16_t s_pal_rgb565[256];

static int vid_init_impl(int width, int height) {
  (void)width; (void)height;
  return 0;
}

static void vid_shutdown_impl(void) {}

static int vid_set_mode_impl(int width, int height) {
  (void)width; (void)height;
  return 0;
}

static void vid_set_palette_impl(rgb_t *pal) {
  for (int i = 0; i < 256; i++) {
    uint16_t r = (uint16_t)(pal[i].r >> 3) & 0x1F;
    uint16_t g = (uint16_t)(pal[i].g >> 2) & 0x3F;
    uint16_t b = (uint16_t)(pal[i].b >> 3) & 0x1F;
    s_pal_rgb565[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  nes_display_set_palette(s_pal_rgb565);
}

const uint16_t *nes_platform_get_palette(void) {
  return s_pal_rgb565;
}

static void vid_clear_impl(uint8 color) { (void)color; }

static bitmap_t *vid_lock_write_impl(void) {
  static uint8_t *fb_screen = NULL;
  if (!fb_screen)
    fb_screen = (uint8_t *)platform_malloc(NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT);
  return bmp_createhw(fb_screen, NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT,
                      NES_SCREEN_WIDTH);
}

static void vid_free_write_impl(int n, rect_t *r) {
  (void)n; (void)r;
}

static viddriver_t s_nes_vid_driver = {
    .name     = "nes",
    .init     = vid_init_impl,
    .shutdown = vid_shutdown_impl,
    .set_mode = vid_set_mode_impl,
    .set_palette = vid_set_palette_impl,
    .clear    = vid_clear_impl,
    .lock_write = vid_lock_write_impl,
    .free_write = vid_free_write_impl,
    .custom_blit = vid_custom_blit,
    .invalidate = false,
};

void osd_getvideoinfo(vidinfo_t *info) {
  info->default_width  = NES_SCREEN_WIDTH;
  info->default_height = NES_VISIBLE_HEIGHT;
  info->driver         = &s_nes_vid_driver;
}

/* ------------------------------------------------------------------ */
/*  OSD Init / Shutdown                                                */
/* ------------------------------------------------------------------ */
static int logprint(const char *s) {
  return printf("%s", s);
}

int osd_init(void) {
  log_chain_logfunc(logprint);

  osd_init_sound();
  nes_display_write(NULL, 0);

  return 0;
}

void osd_shutdown(void) {
  osd_stopsound();
}

/* ------------------------------------------------------------------ */
/*  OSD Main                                                           */
/* ------------------------------------------------------------------ */
int osd_main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  return main_loop("nes_rom", system_nes);
}

/* ------------------------------------------------------------------ */
/*  OSD Utility stubs                                                  */
/* ------------------------------------------------------------------ */
void osd_fullname(char *fullname, const char *shortname) {
  strncpy(fullname, shortname, PATH_MAX);
}

char *osd_newextension(char *string, char *ext) {
  (void)ext;
  return string;
}

int osd_makesnapname(char *filename, int len) {
  (void)filename; (void)len;
  return -1;
}

void osd_getinput(void) {
  const int ev[16] = {
      event_joypad1_select, 0, 0, event_joypad1_start,
      event_joypad1_up, event_joypad1_right,
      event_joypad1_down, event_joypad1_left,
      0, 0, 0, 0,
      event_soft_reset,
      event_joypad1_a, event_joypad1_b, event_hard_reset,
  };
  static int oldb = 0xffff;
  int b = nes_input_to_bits();
  int chg = b ^ oldb;
  oldb = b;
  for (int x = 0; x < 16; x++) {
    if (chg & 1) {
      event_t evh = event_get(ev[x]);
      if (evh)
        evh((b & 1) ? INP_STATE_BREAK : INP_STATE_MAKE);
    }
    chg >>= 1;
    b >>= 1;
  }
}

void osd_getmouse(int *x, int *y, int *button) {
  *x = 0; *y = 0; *button = 0;
}

/* ------------------------------------------------------------------ */
/*  Init / Deinit for preview_nes                                      */
/* ------------------------------------------------------------------ */
void nes_platform_init(const char *rom_path) {
  strlcpy(s_rom_path, rom_path, sizeof(s_rom_path));
  nes_platform_audio_init();
  nes_platform_timer_init();
}

void nes_platform_deinit(void) {
  nes_platform_timer_deinit();
  nes_platform_audio_deinit();
  if (s_lcdfb) {
    platform_free(s_lcdfb);
    s_lcdfb = NULL;
  }
}

/* ------------------------------------------------------------------ */
/*  Save / Load paths                                                  */
/* ------------------------------------------------------------------ */
#define SAVE_DIR  "/sd/esplay/data/nes"

void nes_platform_save_path(const char *rom_path, char *out, size_t out_len) {
  const char *name = strrchr(rom_path, '/');
  if (!name) name = rom_path;
  else name++;
  snprintf(out, out_len, SAVE_DIR "/%s.sav", name);
}

void nes_platform_ensure_save_dir(void) {
  mkdir("/sd/esplay", 0755);
  mkdir("/sd/esplay/data", 0755);
  mkdir(SAVE_DIR, 0755);
}
