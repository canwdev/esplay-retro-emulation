/**
 * Basic WAV player via I2S STD master TX: PCM 16-bit LE or IEEE float 32-bit LE,
 * mono or stereo. Float samples are converted to int16 for I2S.
 * Pin assignments: menuconfig → Audio I2S speaker.
 */

#include "audio.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio";

#if CONFIG_AUDIO_AMP_GPIO >= 0
static void amp_gpio_configure(void) {
  gpio_config_t io = {
      .pin_bit_mask = 1ULL << (unsigned)CONFIG_AUDIO_AMP_GPIO,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);
  gpio_set_level((gpio_num_t)CONFIG_AUDIO_AMP_GPIO, 0);
}

static void amp_enable(void) {
  gpio_set_level((gpio_num_t)CONFIG_AUDIO_AMP_GPIO, 1);
}

static void amp_disable(void) {
  gpio_set_level((gpio_num_t)CONFIG_AUDIO_AMP_GPIO, 0);
}
#else
static void amp_gpio_configure(void) {}
static void amp_enable(void) {}
static void amp_disable(void) {}
#endif

static i2s_chan_handle_t s_tx_chan;

static volatile bool s_stop_requested;
static volatile bool s_playing;
static TaskHandle_t s_play_task;

static int16_t s_stereo_expand[2048];

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

typedef struct {
  uint16_t num_channels;
  uint16_t bits_per_sample;
  uint32_t sample_rate;
  uint32_t data_size;
  long data_offset;
  uint16_t audio_format;
  bool is_ieee_float;
} wav_pcm_info_t;

#define WAV_FORMAT_PCM 0x0001u
#define WAV_FORMAT_IEEE_FLOAT 0x0003u
#define WAV_FORMAT_EXTENSIBLE 0xFFFEu

/** PCM GUID first DWORD (little-endian): {01 00 00 00 ...} */
#define WAV_SUBFORMAT_PCM_LE32 0x00000001u
/** IEEE float GUID first DWORD (LE): {03 00 00 00 ...} */
#define WAV_SUBFORMAT_IEEE_FLOAT_LE32 0x00000003u

static int16_t float_sample_to_i16(float x) {
  if (!isfinite(x))
    x = 0.f;
  if (x > 1.0f)
    x = 1.0f;
  else if (x < -1.0f)
    x = -1.0f;
  return (int16_t)(x * 32767.0f);
}

static esp_err_t wav_load_pcm_info(FILE *f, wav_pcm_info_t *out) {
  uint8_t riff[12];
  if (fread(riff, 1, 12, f) != 12)
    return ESP_FAIL;
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    ESP_LOGW(TAG, "Not RIFF/WAVE");
    return ESP_ERR_NOT_SUPPORTED;
  }

  memset(out, 0, sizeof(*out));

  for (;;) {
    uint8_t cid[4];
    uint8_t szraw[4];
    if (fread(cid, 1, 4, f) != 4 || fread(szraw, 1, 4, f) != 4)
      return ESP_FAIL;
    uint32_t chunk_size = read_u32_le(szraw);

    if (memcmp(cid, "fmt ", 4) == 0) {
      if (chunk_size < 16) {
        ESP_LOGW(TAG, "fmt chunk too small: %" PRIu32, chunk_size);
        return ESP_ERR_NOT_SUPPORTED;
      }
      uint8_t fmt_hdr[40];
      size_t read_fmt = chunk_size > sizeof(fmt_hdr) ? sizeof(fmt_hdr) : chunk_size;
      if (fread(fmt_hdr, 1, read_fmt, f) != read_fmt)
        return ESP_FAIL;
      if (chunk_size > read_fmt) {
        if (fseek(f, (long)(chunk_size - read_fmt), SEEK_CUR) != 0)
          return ESP_FAIL;
      }

      out->audio_format = read_u16_le(fmt_hdr);
      out->num_channels = read_u16_le(fmt_hdr + 2);
      out->sample_rate = read_u32_le(fmt_hdr + 4);
      out->bits_per_sample = read_u16_le(fmt_hdr + 14);

      bool extensible_pcm = false;
      bool extensible_float = false;
      if (out->audio_format == WAV_FORMAT_EXTENSIBLE && chunk_size >= 40) {
        uint32_t sub0 = read_u32_le(fmt_hdr + 24);
        extensible_pcm = (sub0 == WAV_SUBFORMAT_PCM_LE32);
        extensible_float = (sub0 == WAV_SUBFORMAT_IEEE_FLOAT_LE32);
      }
      bool extensible_short_pcm =
          (out->audio_format == WAV_FORMAT_EXTENSIBLE && chunk_size >= 16 &&
           chunk_size < 40 && out->bits_per_sample == 16 &&
           (out->num_channels == 1 || out->num_channels == 2));

      out->is_ieee_float =
          (out->audio_format == WAV_FORMAT_IEEE_FLOAT) || extensible_float;

      bool ok_fmt = (out->audio_format == WAV_FORMAT_PCM) || extensible_pcm ||
                    extensible_short_pcm || out->is_ieee_float;
      if (!ok_fmt) {
        ESP_LOGW(TAG, "Unsupported WAV format tag 0x%04x (need PCM, IEEE "
                      "float, or EXTENSIBLE/PCM|FLOAT)",
                 (unsigned)out->audio_format);
        return ESP_ERR_NOT_SUPPORTED;
      }
    } else if (memcmp(cid, "data", 4) == 0) {
      out->data_offset = ftell(f);
      out->data_size = chunk_size;
      if (fseek(f, (long)chunk_size, SEEK_CUR) != 0)
        return ESP_FAIL;
      break;
    } else {
      if (fseek(f, (long)chunk_size, SEEK_CUR) != 0)
        return ESP_FAIL;
    }
    if (chunk_size & 1) {
      if (fseek(f, 1, SEEK_CUR) != 0)
        return ESP_FAIL;
    }
  }

  if (out->data_offset == 0 || out->sample_rate == 0) {
    ESP_LOGW(TAG, "WAV missing fmt/data or zero rate (data_off=%ld rate=%lu)",
             (long)out->data_offset, (unsigned long)out->sample_rate);
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (out->num_channels != 1 && out->num_channels != 2) {
    ESP_LOGW(TAG, "Need mono or stereo: got %uch format=0x%04x",
             (unsigned)out->num_channels, (unsigned)out->audio_format);
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (out->is_ieee_float) {
    if (out->bits_per_sample != 32) {
      ESP_LOGW(TAG, "IEEE float WAV need 32-bit samples: got %u-bit",
               (unsigned)out->bits_per_sample);
      return ESP_ERR_NOT_SUPPORTED;
    }
  } else if (out->bits_per_sample != 16) {
    ESP_LOGW(TAG, "PCM WAV need 16-bit samples: got %u-bit",
             (unsigned)out->bits_per_sample);
    return ESP_ERR_NOT_SUPPORTED;
  }

  return ESP_OK;
}

static void i2s_teardown(void) {
  if (!s_tx_chan)
    return;
  i2s_channel_disable(s_tx_chan);
  i2s_del_channel(s_tx_chan);
  s_tx_chan = NULL;
}

static esp_err_t i2s_setup(uint32_t sample_rate_hz) {
  i2s_teardown();

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 6;
  chan_cfg.dma_frame_num = 256;

  esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
    return err;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)CONFIG_AUDIO_I2S_BCLK_GPIO,
              .ws = (gpio_num_t)CONFIG_AUDIO_I2S_WS_GPIO,
              .dout = (gpio_num_t)CONFIG_AUDIO_I2S_DOUT_GPIO,
              .din = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
    i2s_teardown();
    return err;
  }

  err = i2s_channel_enable(s_tx_chan);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    i2s_teardown();
    return err;
  }

  return ESP_OK;
}

static esp_err_t stream_wav_body(FILE *f, const wav_pcm_info_t *info) {
  if (fseek(f, info->data_offset, SEEK_SET) != 0)
    return ESP_FAIL;

  uint8_t raw[1024];
  uint32_t remaining = info->data_size;
  const size_t frame_bytes =
      (size_t)info->num_channels * (info->is_ieee_float ? 4u : 2u);

  while (remaining > 0 && !s_stop_requested) {
    size_t want = remaining > sizeof(raw) ? sizeof(raw) : remaining;
    size_t rd = fread(raw, 1, want, f);
    if (rd == 0)
      break;

    const uint8_t *write_ptr = raw;
    size_t write_bytes = rd;

    if (info->is_ieee_float) {
      size_t max_frames = sizeof(s_stereo_expand) / sizeof(int16_t) / 2;
      size_t frames = rd / frame_bytes;
      if (frames > max_frames)
        frames = max_frames;
      if (info->num_channels == 1) {
        for (size_t i = 0; i < frames; i++) {
          float fp;
          memcpy(&fp, raw + i * sizeof(float), sizeof(float));
          int16_t s = float_sample_to_i16(fp);
          s_stereo_expand[i * 2] = s;
          s_stereo_expand[i * 2 + 1] = s;
        }
      } else {
        for (size_t i = 0; i < frames; i++) {
          float fl, fr;
          memcpy(&fl, raw + i * 8, sizeof(float));
          memcpy(&fr, raw + i * 8 + sizeof(float), sizeof(float));
          s_stereo_expand[i * 2] = float_sample_to_i16(fl);
          s_stereo_expand[i * 2 + 1] = float_sample_to_i16(fr);
        }
      }
      write_ptr = (const uint8_t *)s_stereo_expand;
      write_bytes = frames * sizeof(int16_t) * 2;
    } else if (info->num_channels == 1) {
      size_t samples = rd / 2;
      if (samples > sizeof(s_stereo_expand) / sizeof(int16_t) / 2)
        samples = sizeof(s_stereo_expand) / sizeof(int16_t) / 2;
      const int16_t *mono = (const int16_t *)raw;
      for (size_t i = 0; i < samples; i++) {
        s_stereo_expand[i * 2] = mono[i];
        s_stereo_expand[i * 2 + 1] = mono[i];
      }
      write_ptr = (const uint8_t *)s_stereo_expand;
      write_bytes = samples * 4;
    }

    size_t wr_total = 0;
    while (wr_total < write_bytes && !s_stop_requested) {
      size_t nwritten = 0;
      esp_err_t err = i2s_channel_write(s_tx_chan, write_ptr + wr_total,
                                        write_bytes - wr_total, &nwritten,
                                        pdMS_TO_TICKS(500));
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s write err %s", esp_err_to_name(err));
        return err;
      }
      if (nwritten == 0)
        break;
      wr_total += nwritten;
    }

    remaining -= (uint32_t)rd;
  }

  return ESP_OK;
}

static void play_task(void *arg) {
  char *path = (char *)arg;
  s_playing = true;
  s_stop_requested = false;

  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "fopen failed: %s errno=%d", path, errno);
    free(path);
    goto done;
  }
  free(path);

  wav_pcm_info_t wi;
  esp_err_t err = wav_load_pcm_info(f, &wi);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WAV parse failed: %s", esp_err_to_name(err));
    fclose(f);
    goto done;
  }

  err = i2s_setup(wi.sample_rate);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
    fclose(f);
    goto done;
  }

  amp_enable();

  ESP_LOGI(TAG, "Playing %lu Hz, %u ch, %s, %lu bytes data",
           (unsigned long)wi.sample_rate, wi.num_channels,
           wi.is_ieee_float ? "float32" : "pcm16", (unsigned long)wi.data_size);

  stream_wav_body(f, &wi);
  fclose(f);

done:
  amp_disable();
  i2s_teardown();
  s_playing = false;
  s_play_task = NULL;
  vTaskDelete(NULL);
}

void audio_init(void) {
  amp_gpio_configure();
  ESP_LOGI(TAG, "I2S pins BCLK=%d WS=%d DOUT=%d", CONFIG_AUDIO_I2S_BCLK_GPIO,
           CONFIG_AUDIO_I2S_WS_GPIO, CONFIG_AUDIO_I2S_DOUT_GPIO);
#if CONFIG_AUDIO_AMP_GPIO >= 0
  ESP_LOGI(TAG, "Speaker amp SHDN GPIO=%d (high=on)", CONFIG_AUDIO_AMP_GPIO);
#else
  ESP_LOGI(TAG, "Speaker amp GPIO disabled (headphones / external DAC only)");
#endif
}

void audio_stop_playback(void) { s_stop_requested = true; }

bool audio_is_playing(void) { return s_playing; }

void audio_play_wav_async(const char *path) {
  if (!path)
    return;

  audio_stop_playback();
  TickType_t wait = 0;
  while (s_play_task && wait < pdMS_TO_TICKS(2000)) {
    vTaskDelay(pdMS_TO_TICKS(10));
    wait += pdMS_TO_TICKS(10);
  }

  size_t len = strlen(path) + 1;
  char *copy = (char *)malloc(len);
  if (!copy)
    return;
  memcpy(copy, path, len);

  s_stop_requested = false;
  if (xTaskCreate(play_task, "wav_play", 4096, copy, 5, &s_play_task) !=
      pdPASS) {
    ESP_LOGE(TAG, "xTaskCreate failed");
    free(copy);
    s_play_task = NULL;
  }
}
