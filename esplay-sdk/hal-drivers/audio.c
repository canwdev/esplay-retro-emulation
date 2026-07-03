#include "audio.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "audio";

#define AUDIO_TASK_STACK_WORDS   32768
#define AUDIO_TASK_CREATE_RETRY   12
#define AUDIO_TASK_CREATE_WAIT_MS 20

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

static void amp_enable(void) { gpio_set_level((gpio_num_t)CONFIG_AUDIO_AMP_GPIO, 1); }
static void amp_disable(void) { gpio_set_level((gpio_num_t)CONFIG_AUDIO_AMP_GPIO, 0); }
#else
static void amp_gpio_configure(void) {}
static void amp_enable(void) {}
static void amp_disable(void) {}
#endif

static i2s_chan_handle_t s_tx_chan;
static bool s_i2s_enabled;

static volatile bool s_stop_requested;
static volatile bool s_playing;
static volatile bool s_paused;
static volatile uint8_t s_volume = 50;
static volatile int32_t s_seek_delta_sec;
static volatile bool s_seek_pending;
static TaskHandle_t s_play_task;
static SemaphoreHandle_t s_play_task_exited_sem;

static int16_t s_stereo_expand[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
static int16_t s_silence_buf[512];
static mp3dec_t *s_mp3_dec;
static uint8_t *s_mp3_buf;

static uint32_t s_position_ms;
static uint32_t s_duration_ms;
static uint32_t s_sample_rate;
static bool s_track_is_mp3;
static volatile audio_track_type_t s_track_type;
static volatile uint16_t s_track_channels;
static volatile uint16_t s_track_bits_per_sample;
static volatile uint16_t s_track_bitrate_kbps;
static volatile bool s_track_is_float;
static volatile bool s_track_mp3_vbr;

typedef struct {
  uint16_t num_channels;
  uint16_t bits_per_sample;
  uint32_t sample_rate;
  uint32_t data_size;
  long data_offset;
  uint16_t audio_format;
  bool is_ieee_float;
} wav_pcm_info_t;

static wav_pcm_info_t s_active_wi;

static bool audio_wait_while_paused(void);

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

#define WAV_FORMAT_PCM 0x0001u
#define WAV_FORMAT_IEEE_FLOAT 0x0003u
#define WAV_FORMAT_EXTENSIBLE 0xFFFEu
#define WAV_SUBFORMAT_PCM_LE32 0x00000001u
#define WAV_SUBFORMAT_IEEE_FLOAT_LE32 0x00000003u

static int16_t apply_volume_i16(int16_t sample) {
  int32_t v = (int32_t)s_volume;
  if (v <= 0)
    return 0;
  if (v >= 100)
    return sample;

  /* Non-linear volume curve: gain = (pct / 100)^2
   * This matches human loudness perception — low values are much quieter,
   * giving finer control in the quiet-to-medium range. */
  float gain = (float)v / 100.0f;
  gain = gain * gain;
  float fs = (float)sample * gain;
  int32_t scaled = (int32_t)(fs >= 0.0f ? fs + 0.5f : fs - 0.5f);

  if (scaled > 32767)
    scaled = 32767;
  else if (scaled < -32768)
    scaled = -32768;
  return (int16_t)scaled;
}

static int16_t float_sample_to_i16(float x) {
  if (!isfinite(x))
    x = 0.f;
  if (x > 1.0f)
    x = 1.0f;
  else if (x < -1.0f)
    x = -1.0f;
  return apply_volume_i16((int16_t)(x * 32767.0f));
}

static bool path_has_ext(const char *path, const char *ext) {
  const char *dot = strrchr(path, '.');
  return dot != NULL && strcasecmp(dot, ext) == 0;
}

static bool path_is_mp3(const char *path) { return path_has_ext(path, ".mp3"); }

static void i2s_teardown(void) {
  if (!s_tx_chan)
    return;
  if (s_i2s_enabled) {
    i2s_channel_disable(s_tx_chan);
    s_i2s_enabled = false;
  }
  i2s_del_channel(s_tx_chan);
  s_tx_chan = NULL;
}

static esp_err_t i2s_setup(uint32_t sample_rate_hz) {
  if (sample_rate_hz < 8000 || sample_rate_hz > 96000) {
    ESP_LOGW(TAG, "Invalid sample rate: %" PRIu32, sample_rate_hz);
    return ESP_ERR_INVALID_ARG;
  }
  ESP_LOGI(TAG, "I2S setup sample rate=%" PRIu32, sample_rate_hz);

  if (s_tx_chan && s_sample_rate == sample_rate_hz)
    return ESP_OK;

  i2s_teardown();
  s_sample_rate = sample_rate_hz;

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 8;
  chan_cfg.dma_frame_num = 512;

  esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
  if (err != ESP_OK)
    return err;

  i2s_std_config_t std_cfg = {
      .clk_cfg =
          {
              .sample_rate_hz = sample_rate_hz,
              /* APLL has far lower jitter than the default APB divider for audio
               * frequencies (44100 / 48000 Hz), giving smoother playback. */
              .clk_src        = I2S_CLK_SRC_APLL,
              .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
          },
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
    i2s_teardown();
    return err;
  }

  err = i2s_channel_enable(s_tx_chan);
  if (err != ESP_OK) {
    i2s_teardown();
    return err;
  }
  s_i2s_enabled = true;

  return ESP_OK;
}

static esp_err_t i2s_write_pcm(const int16_t *samples, size_t sample_count) {
  if (!s_tx_chan || sample_count == 0)
    return ESP_OK;

  const uint8_t *ptr = (const uint8_t *)samples;
  size_t bytes = sample_count * sizeof(int16_t);
  size_t written_total = 0;

  while (written_total < bytes && !s_stop_requested) {
    if (audio_wait_while_paused())
      return ESP_OK;

    size_t nwritten = 0;
    esp_err_t err =
        i2s_channel_write(s_tx_chan, ptr + written_total, bytes - written_total,
                          &nwritten, pdMS_TO_TICKS(500));
    if (err != ESP_OK)
      return err;
    if (nwritten == 0)
      break;
    written_total += nwritten;
  }

  return ESP_OK;
}

static esp_err_t i2s_write_stereo_from_mono(const int16_t *mono, size_t frames) {
  if (frames > MINIMP3_MAX_SAMPLES_PER_FRAME)
    frames = MINIMP3_MAX_SAMPLES_PER_FRAME;

  /* Expand mono → stereo in REVERSE order so that in-place use
   * (mono == s_stereo_expand) does not overwrite unread source samples.
   * Forward order: writing s_stereo_expand[2i+1] clobbers mono[i+1]. */
  for (size_t i = frames; i > 0; i--) {
    int16_t s = apply_volume_i16(mono[i - 1]);
    s_stereo_expand[(i - 1) * 2]     = s;
    s_stereo_expand[(i - 1) * 2 + 1] = s;
  }

  return i2s_write_pcm(s_stereo_expand, frames * 2);
}

static esp_err_t i2s_write_stereo_interleaved(int16_t *stereo, size_t samples) {
  if (samples > MINIMP3_MAX_SAMPLES_PER_FRAME * 2)
    samples = MINIMP3_MAX_SAMPLES_PER_FRAME * 2;

  for (size_t i = 0; i < samples; i++)
    stereo[i] = apply_volume_i16(stereo[i]);

  return i2s_write_pcm(stereo, samples);
}

static bool audio_wait_while_paused(void) {
  if (!s_paused || s_stop_requested)
    return s_seek_pending;

  while (s_paused && !s_stop_requested) {
    if (s_seek_pending)
      return true;
    /* Keep I2S fed with silence so headphone path doesn't buzz on pause. */
    if (s_tx_chan && s_i2s_enabled) {
      size_t nwritten = 0;
      i2s_channel_write(s_tx_chan, s_silence_buf, sizeof(s_silence_buf),
                        &nwritten, pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  return s_seek_pending;
}

static esp_err_t wav_load_pcm_info(FILE *f, wav_pcm_info_t *out) {
  uint8_t riff[12];
  if (fread(riff, 1, 12, f) != 12)
    return ESP_FAIL;
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
    return ESP_ERR_NOT_SUPPORTED;

  memset(out, 0, sizeof(*out));

  for (;;) {
    uint8_t cid[4];
    uint8_t szraw[4];
    if (fread(cid, 1, 4, f) != 4 || fread(szraw, 1, 4, f) != 4)
      return ESP_FAIL;
    uint32_t chunk_size = read_u32_le(szraw);

    if (memcmp(cid, "fmt ", 4) == 0) {
      if (chunk_size < 16)
        return ESP_ERR_NOT_SUPPORTED;
      uint8_t fmt_hdr[40];
      size_t read_fmt =
          chunk_size > sizeof(fmt_hdr) ? sizeof(fmt_hdr) : chunk_size;
      if (fread(fmt_hdr, 1, read_fmt, f) != read_fmt)
        return ESP_FAIL;
      if (chunk_size > read_fmt &&
          fseek(f, (long)(chunk_size - read_fmt), SEEK_CUR) != 0)
        return ESP_FAIL;

      out->audio_format = read_u16_le(fmt_hdr);
      out->num_channels = read_u16_le(fmt_hdr + 2);
      out->sample_rate = read_u32_le(fmt_hdr + 4);
      out->bits_per_sample = read_u16_le(fmt_hdr + 14);

      bool ext_float = false;
      if (out->audio_format == WAV_FORMAT_EXTENSIBLE && chunk_size >= 40)
        ext_float = read_u32_le(fmt_hdr + 24) == WAV_SUBFORMAT_IEEE_FLOAT_LE32;

      out->is_ieee_float =
          (out->audio_format == WAV_FORMAT_IEEE_FLOAT) || ext_float;

      bool ok = (out->audio_format == WAV_FORMAT_PCM) || out->is_ieee_float ||
                (out->audio_format == WAV_FORMAT_EXTENSIBLE &&
                 out->bits_per_sample == 16);
      if (!ok)
        return ESP_ERR_NOT_SUPPORTED;
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
    if (chunk_size & 1 && fseek(f, 1, SEEK_CUR) != 0)
      return ESP_FAIL;
  }

  if (!out->data_offset || !out->sample_rate)
    return ESP_ERR_NOT_SUPPORTED;
  if (out->num_channels != 1 && out->num_channels != 2)
    return ESP_ERR_NOT_SUPPORTED;
  if (out->is_ieee_float) {
    if (out->bits_per_sample != 32)
      return ESP_ERR_NOT_SUPPORTED;
  } else if (out->bits_per_sample != 16) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  return ESP_OK;
}

static size_t wav_frame_bytes(const wav_pcm_info_t *info) {
  return (size_t)info->num_channels * (info->is_ieee_float ? 4u : 2u);
}

static uint32_t wav_bytes_to_ms(const wav_pcm_info_t *info, uint32_t bytes) {
  size_t fb = wav_frame_bytes(info);
  if (!info->sample_rate || !fb)
    return 0;
  return (uint32_t)((bytes / fb) * 1000ULL / info->sample_rate);
}

static uint32_t s_wav_position_bytes;

static bool mp3_hz_supported(int hz) {
  return hz == 8000 || hz == 11025 || hz == 12000 || hz == 16000 ||
         hz == 22050 || hz == 24000 || hz == 32000 || hz == 44100 ||
         hz == 48000;
}

static bool mp3_frame_info_valid(const mp3dec_frame_info_t *info, int samples) {
  if (!info || samples <= 0)
    return false;
  if (info->frame_bytes <= 0)
    return false;
  if (info->channels != 1 && info->channels != 2)
    return false;
  if (!mp3_hz_supported(info->hz))
    return false;
  if (info->bitrate_kbps <= 0 || info->bitrate_kbps > 320)
    return false;
  return true;
}

static bool wav_apply_seek(FILE *f, const wav_pcm_info_t *info, int32_t delta_sec) {
  if (!delta_sec)
    return true;

  size_t fb = wav_frame_bytes(info);
  int64_t delta_bytes =
      (int64_t)delta_sec * (int64_t)info->sample_rate * (int64_t)fb;
  int64_t new_pos = (int64_t)s_wav_position_bytes + delta_bytes;
  if (new_pos < 0)
    new_pos = 0;
  if ((uint64_t)new_pos > info->data_size)
    new_pos = info->data_size;

  new_pos -= new_pos % (int64_t)fb;
  s_wav_position_bytes = (uint32_t)new_pos;
  s_position_ms = wav_bytes_to_ms(info, s_wav_position_bytes);
  return fseek(f, info->data_offset + (long)s_wav_position_bytes, SEEK_SET) == 0;
}

static esp_err_t stream_wav_body(FILE *f, const wav_pcm_info_t *info) {
  if (fseek(f, info->data_offset + (long)s_wav_position_bytes, SEEK_SET) != 0)
    return ESP_FAIL;

  uint32_t remaining = info->data_size - s_wav_position_bytes;
  /* Bigger read chunk reduces SD/FAT read jitter on WAV playback. */
  uint8_t raw[4096];
  const size_t frame_bytes = wav_frame_bytes(info);

  while (remaining > 0 && !s_stop_requested) {
    if (audio_wait_while_paused()) {
      if (s_seek_pending) {
        int32_t delta = s_seek_delta_sec;
        s_seek_pending = false;
        wav_apply_seek(f, info, delta);
        remaining = info->data_size - s_wav_position_bytes;
      }
      continue;
    }

    size_t want = remaining > sizeof(raw) ? sizeof(raw) : remaining;
    size_t rd = fread(raw, 1, want, f);
    if (rd == 0)
      break;

    esp_err_t err = ESP_OK;

    if (info->is_ieee_float) {
      size_t frames = rd / frame_bytes;
      if (info->num_channels == 1) {
        for (size_t i = 0; i < frames; i++) {
          float fp;
          memcpy(&fp, raw + i * sizeof(float), sizeof(float));
          int16_t s = float_sample_to_i16(fp);
          s_stereo_expand[i * 2] = s;
          s_stereo_expand[i * 2 + 1] = s;
        }
        err = i2s_write_pcm(s_stereo_expand, frames * 2);
      } else {
        for (size_t i = 0; i < frames; i++) {
          float fl, fr;
          memcpy(&fl, raw + i * 8, sizeof(float));
          memcpy(&fr, raw + i * 8 + sizeof(float), sizeof(float));
          s_stereo_expand[i * 2] = float_sample_to_i16(fl);
          s_stereo_expand[i * 2 + 1] = float_sample_to_i16(fr);
        }
        err = i2s_write_pcm(s_stereo_expand, frames * 2);
      }
    } else if (info->num_channels == 1) {
      err = i2s_write_stereo_from_mono((const int16_t *)raw, rd / 2);
    } else {
      memcpy(s_stereo_expand, raw, rd);
      err = i2s_write_stereo_interleaved(s_stereo_expand, rd / sizeof(int16_t));
    }

    if (err != ESP_OK)
      return err;

    s_wav_position_bytes += (uint32_t)rd;
    s_position_ms = wav_bytes_to_ms(info, s_wav_position_bytes);
    remaining -= (uint32_t)rd;
  }

  return ESP_OK;
}

static uint32_t mp3_samples_to_ms(uint64_t samples, uint32_t hz) {
  if (!hz)
    return 0;
  return (uint32_t)(samples * 1000ULL / hz);
}

static esp_err_t mp3_skip_to_ms(FILE *f, mp3dec_t *dec, uint32_t target_ms,
                                uint8_t *buf, size_t buf_sz, size_t *buf_len,
                                uint64_t *decoded_samples) {
  if (fseek(f, 0, SEEK_SET) != 0)
    return ESP_FAIL;

  *buf_len = 0;
  *decoded_samples = 0;
  mp3dec_init(dec);

  mp3dec_frame_info_t info;
  while (!s_stop_requested && s_position_ms < target_ms) {
    if (*buf_len < buf_sz / 2) {
      size_t n = fread(buf + *buf_len, 1, buf_sz - *buf_len, f);
      if (n == 0)
        break;
      *buf_len += n;
    }

    int consumed = 0;
    while (consumed < (int)*buf_len) {
      int samples = mp3dec_decode_frame(dec, buf + consumed,
                                        (int)*buf_len - consumed,
                                        s_stereo_expand, &info);
      if (info.frame_bytes <= 0)
        break;
      consumed += info.frame_bytes;

      if (samples > 0) {
        if (info.hz)
          s_sample_rate = (uint32_t)info.hz;
        *decoded_samples += (uint64_t)samples;
        s_position_ms = mp3_samples_to_ms(*decoded_samples, s_sample_rate);
        if (s_position_ms >= target_ms)
          goto done;
      }
    }

    if (consumed > 0) {
      memmove(buf, buf + consumed, *buf_len - (size_t)consumed);
      *buf_len -= (size_t)consumed;
    } else {
      break;
    }
  }

done:
  return ESP_OK;
}

/* Proportional-offset fast seek for CBR (and approximate for VBR) MP3.
 * Calculates target byte position as target_ms/duration_ms × file_size, then
 * scans forward up to 4 KB to find the next MP3 sync word.  Completes in
 * < 5 ms regardless of file length.
 *
 * file_size  – total byte length of the MP3 file (passed by stream_mp3_body)
 * cur_buf_len – bytes currently buffered in s_mp3_buf but not yet decoded;
 *              used together with ftell() to estimate the decoded byte position
 *              when s_duration_ms is zero (VBR / unknown bitrate). */
static esp_err_t mp3_fast_seek(FILE *f, mp3dec_t *dec, uint32_t target_ms,
                               long file_size, size_t cur_buf_len,
                               size_t *out_buf_len, uint64_t *decoded_samples) {
  if (s_sample_rate == 0 || file_size <= 0)
    return ESP_ERR_NOT_SUPPORTED;

  /* Determine the effective duration for the proportional offset. */
  uint32_t est_duration = s_duration_ms;
  if (est_duration == 0) {
    /* Fallback: derive duration from (decoded byte position) / (playback ms).
     * decoded_byte ≈ ftell(f) − bytes still in the read-ahead buffer. */
    if (s_position_ms == 0)
      return ESP_ERR_NOT_SUPPORTED;
    long read_head = ftell(f);
    if (read_head < 0)
      return ESP_FAIL;
    long decoded_byte = read_head - (long)cur_buf_len;
    if (decoded_byte <= 0)
      return ESP_ERR_NOT_SUPPORTED;
    est_duration = (uint32_t)((uint64_t)(unsigned long)file_size *
                              (uint64_t)s_position_ms / (uint64_t)decoded_byte);
    if (est_duration == 0)
      return ESP_ERR_NOT_SUPPORTED;
  }

  long byte_pos = (long)((uint64_t)target_ms * (uint64_t)(unsigned long)file_size /
                         (uint64_t)est_duration);
  if (byte_pos < 0)
    byte_pos = 0;
  if (byte_pos > file_size - 4)
    byte_pos = file_size > 4 ? file_size - 4 : 0;

  if (fseek(f, byte_pos, SEEK_SET) != 0)
    return ESP_FAIL;

  /* Scan forward for the nearest sync word (0xFF 0xEx). */
  uint8_t scan[256];
  size_t  scanned = 0;
  while (scanned < 4096) {
    size_t n = fread(scan, 1, sizeof(scan), f);
    if (n < 2)
      break;
    for (size_t i = 0; i + 1 < n; i++) {
      if (scan[i] == 0xFF && (scan[i + 1] & 0xE0) == 0xE0) {
        long sync_pos = byte_pos + (long)(scanned + i);
        if (fseek(f, sync_pos, SEEK_SET) != 0)
          return ESP_FAIL;
        mp3dec_init(dec);
        *out_buf_len     = 0;
        *decoded_samples = (uint64_t)target_ms * (uint64_t)s_sample_rate / 1000;
        s_position_ms    = target_ms;
        return ESP_OK;
      }
    }
    scanned += n;
  }
  return ESP_ERR_NOT_FOUND;
}

#define MP3_IO_BUF_SZ 16384

static esp_err_t stream_mp3_body(FILE *f, long file_size) {
  mp3dec_init(s_mp3_dec);

  size_t   buf_len         = 0;
  uint64_t decoded_samples = 0;
  bool     rate_set        = false;
  bool     got_audio_frame = false;
  bool     eof_reached     = false;

  if (fseek(f, 0, SEEK_SET) != 0)
    return ESP_FAIL;

  while (!s_stop_requested) {
    /* ---- pause / seek ---- */
    if (audio_wait_while_paused()) {
      if (s_seek_pending) {
        int32_t delta  = s_seek_delta_sec;
        s_seek_pending = false;
        int64_t target = (int64_t)s_position_ms + (int64_t)delta * 1000;
        if (target < 0)
          target = 0;
        if ((uint32_t)target > s_duration_ms)
          target = s_duration_ms;
        eof_reached = false;
        /* Stop I2S *before* seek so the DMA cannot loop stale audio into the
         * DAC ("machine gun" artifact).  amp_disable cuts the speaker amp;
         * i2s_teardown stops the I2S clock entirely, silencing UDA1334 too. */
        amp_disable();
        i2s_teardown();

        if (mp3_fast_seek(f, s_mp3_dec, (uint32_t)target, file_size, buf_len,
                          &buf_len, &decoded_samples) != ESP_OK)
          mp3_skip_to_ms(f, s_mp3_dec, (uint32_t)target, s_mp3_buf,
                         MP3_IO_BUF_SZ, &buf_len, &decoded_samples);

        /* Restart I2S with a fresh DMA ring buffer.  Pre-fill it with silence
         * so the amp comes up cleanly with no pop or audio-position artifact. */
        if (s_sample_rate > 0 && !s_stop_requested) {
          i2s_setup(s_sample_rate);
          for (int sj = 0; sj < 4 && s_tx_chan && !s_stop_requested; sj++) {
            size_t nw = 0;
            i2s_channel_write(s_tx_chan, s_silence_buf, sizeof(s_silence_buf),
                              &nw, pdMS_TO_TICKS(50));
          }
        }
        amp_enable();
      }
      continue;
    }

    /* ---- incremental IO refill ----
     * Called every iteration so each fread only covers the bytes consumed by
     * the previous frame (~430 B for 128 kbps).  The FatFS sector cache
     * services these tiny reads in < 1 ms, eliminating the 10–50 ms one-shot
     * fread that previously stalled the loop while the DMA drained. */
    if (!eof_reached && buf_len < MP3_IO_BUF_SZ) {
      size_t n = fread(s_mp3_buf + buf_len, 1, MP3_IO_BUF_SZ - buf_len, f);
      if (n == 0)
        eof_reached = true;
      else
        buf_len += n;
    }
    if (buf_len == 0)
      break;

    /* ---- decode ONE frame ---- */
    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(s_mp3_dec, s_mp3_buf, (int)buf_len,
                                      s_stereo_expand, &info);

    if (info.frame_bytes <= 0) {
      /* No valid frame header in current data. */
      if (buf_len >= 1024) {
        /* Skip one byte (sync recovery for corrupt / non-MP3 data). */
        memmove(s_mp3_buf, s_mp3_buf + 1, buf_len - 1);
        buf_len -= 1;
      } else if (eof_reached) {
        break; /* nothing left to decode */
      }
      /* else: need more data – loop back to fread */
      continue;
    }

    /* Shift the consumed frame bytes out of the front of the buffer. */
    memmove(s_mp3_buf, s_mp3_buf + info.frame_bytes,
            buf_len - (size_t)info.frame_bytes);
    buf_len -= (size_t)info.frame_bytes;

    if (!mp3_frame_info_valid(&info, samples))
      continue;
    got_audio_frame = true;
    if (s_track_channels == 0)
      s_track_channels = (uint16_t)info.channels;
    if (info.bitrate_kbps > 0) {
      if (s_track_bitrate_kbps == 0)
        s_track_bitrate_kbps = (uint16_t)info.bitrate_kbps;
      else if (s_track_bitrate_kbps != (uint16_t)info.bitrate_kbps)
        s_track_mp3_vbr = true;
    }

    /* ---- I2S setup on first valid frame ---- */
    if (!rate_set) {
      esp_err_t err = i2s_setup((uint32_t)info.hz);
      ESP_LOGI(TAG, "i2s_setup hz=%d result=%s", info.hz, esp_err_to_name(err));
      if (err != ESP_OK)
        return err;
      s_sample_rate = (uint32_t)info.hz;
      rate_set      = true;
      amp_enable();
      /* CBR duration estimate: duration_ms = file_bytes * 8 / bitrate_kbps. */
      if (file_size > 0 && info.bitrate_kbps > 0)
        s_duration_ms = (uint32_t)((uint64_t)(unsigned long)file_size * 8ULL /
                                   (uint64_t)info.bitrate_kbps);
    } else if ((uint32_t)info.hz != s_sample_rate) {
      /* Drop frames with a different sample rate to avoid speed artifacts. */
      continue;
    }

    /* ---- write to I2S (blocks ≤ ~26 ms waiting for DMA room) ---- */
    esp_err_t err;
    if (info.channels == 1)
      err = i2s_write_stereo_from_mono(s_stereo_expand, (size_t)samples);
    else
      err = i2s_write_stereo_interleaved(s_stereo_expand, (size_t)samples * 2);
    if (err != ESP_OK)
      return err;

    decoded_samples += (uint64_t)samples;
    s_position_ms    = mp3_samples_to_ms(decoded_samples, s_sample_rate);
  }

  if (!got_audio_frame) {
    ESP_LOGW(TAG, "Invalid MP3 stream");
    return ESP_ERR_NOT_SUPPORTED;
  }

  return ESP_OK;
}

static esp_err_t play_wav_file(FILE *f) {
  wav_pcm_info_t wi;
  esp_err_t err = wav_load_pcm_info(f, &wi);
  if (err != ESP_OK)
    return err;

  s_active_wi = wi;
  s_track_is_mp3 = false;
  s_track_type = AUDIO_TRACK_TYPE_WAV;
  s_track_channels = wi.num_channels;
  s_track_bits_per_sample = wi.bits_per_sample;
  s_track_bitrate_kbps = 0;
  s_track_is_float = wi.is_ieee_float;
  s_track_mp3_vbr = false;
  s_wav_position_bytes = 0;
  s_duration_ms = wav_bytes_to_ms(&wi, wi.data_size);
  s_position_ms = 0;

  err = i2s_setup(wi.sample_rate);
  if (err != ESP_OK)
    return err;

  amp_enable();
  err = stream_wav_body(f, &wi);
  return err;
}

static esp_err_t play_mp3_file(FILE *f) {
  if (fseek(f, 0, SEEK_END) != 0)
    return ESP_FAIL;
  long file_size = ftell(f);
  if (file_size <= 0)
    return ESP_FAIL;
  if (fseek(f, 0, SEEK_SET) != 0)
    return ESP_FAIL;

  s_track_is_mp3 = true;
  s_track_type = AUDIO_TRACK_TYPE_MP3;
  s_track_channels = 0;
  s_track_bits_per_sample = 16;
  s_track_bitrate_kbps = 0;
  s_track_is_float = false;
  s_track_mp3_vbr = false;
  s_sample_rate = 0;
  s_duration_ms = 0;
  s_position_ms = 0;

  /* amp_enable is deferred to stream_mp3_body after i2s_setup to avoid
   * amplifier noise during I2S initialization (mirrors the WAV path). */
  esp_err_t err = stream_mp3_body(f, file_size);
  if (err != ESP_OK)
    return err;

  if (s_sample_rate > 0)
    ESP_LOGI(TAG, "MP3 streaming sample rate=%" PRIu32, s_sample_rate);

  /* Duration fallback: CBR estimate only if we know bitrate from first valid frame. */
  if (s_duration_ms == 0 && file_size > 0 && s_position_ms > 0)
    s_duration_ms = s_position_ms;

  return ESP_OK;
}

static void play_task(void *arg) {
  char *path = (char *)arg;
  s_playing = true;
  s_stop_requested = false;
  s_paused = false;
  s_position_ms = 0;
  s_duration_ms = 0;

  ESP_LOGI(TAG, "play_task START path=%s playing=%d", path, (int)s_playing);

  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "play_task fopen failed: %s errno=%d", path, errno);
    free(path);
    goto done;
  }

  bool is_mp3 = path_is_mp3(path);
  free(path);

  ESP_LOGI(TAG, "play_task before decode: is_mp3=%d s_stop=%d", is_mp3, (int)s_stop_requested);

  if (is_mp3 && (!s_mp3_dec || !s_mp3_buf)) {
    ESP_LOGE(TAG, "MP3 buffers not initialized");
    fclose(f);
    goto done;
  }

  esp_err_t err = ESP_ERR_NOT_SUPPORTED;
  if (is_mp3)
    err = play_mp3_file(f);
  else
    err = play_wav_file(f);

  ESP_LOGI(TAG, "play_task after decode: err=%s s_stop=%d", esp_err_to_name(err), (int)s_stop_requested);

  if (err == ESP_ERR_NOT_SUPPORTED)
    ESP_LOGE(TAG, "Unsupported audio format");
  else if (err != ESP_OK)
    ESP_LOGE(TAG, "Playback failed: %s", esp_err_to_name(err));

  fclose(f);

done:
  amp_disable();
  i2s_teardown();
  s_playing = false;
  s_paused = false;
  s_play_task = NULL;
  if (s_play_task_exited_sem) {
    xSemaphoreGive(s_play_task_exited_sem);
    s_play_task_exited_sem = NULL;
  }
  ESP_LOGI(TAG, "play_task DONE playing=%d", (int)s_playing);
  vTaskDelete(NULL);
}

void audio_init(void) {
  amp_gpio_configure();
  s_volume = 50;
  memset(s_silence_buf, 0, sizeof(s_silence_buf));

  if (!s_mp3_dec)
    s_mp3_dec = (mp3dec_t *)calloc(1, sizeof(mp3dec_t));
  if (!s_mp3_buf)
    s_mp3_buf = (uint8_t *)malloc(MP3_IO_BUF_SZ);
}

static void audio_start_async(char *path_copy) {
  s_stop_requested = false;
  s_paused = false;
  for (int attempt = 0; attempt < AUDIO_TASK_CREATE_RETRY; attempt++) {
    if (xTaskCreate(play_task, "audio_play", AUDIO_TASK_STACK_WORDS, path_copy,
                    10, &s_play_task) == pdPASS) {
      ESP_LOGI(TAG, "audio_start_async: task created, s_play_task=%p attempt=%d",
               s_play_task, attempt + 1);
      return;
    }
    ESP_LOGW(TAG, "audio_start_async: xTaskCreate retry %d/%d", attempt + 1,
             AUDIO_TASK_CREATE_RETRY);
    vTaskDelay(pdMS_TO_TICKS(AUDIO_TASK_CREATE_WAIT_MS));
  }
  ESP_LOGE(TAG, "audio_start_async: xTaskCreate FAILED");
  free(path_copy);
  s_play_task = NULL;
}

void audio_stop_playback(void) { s_stop_requested = true; }

bool audio_is_playing(void) { return s_playing; }

void audio_set_volume(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  s_volume = pct;
}

uint8_t audio_get_volume(void) { return s_volume; }

void audio_pause(void) {
  if (s_playing && !s_paused) {
    s_paused = true;
    amp_disable();
  }
}

void audio_resume(void) {
  if (s_playing && s_paused) {
    s_paused = false;
    amp_enable();
  }
}

void audio_toggle_pause(void) {
  if (!s_playing)
    return;
  if (s_paused)
    audio_resume();
  else
    audio_pause();
}

bool audio_is_paused(void) { return s_paused; }

void audio_seek_seconds(int delta) {
  if (!s_playing)
    return;
  s_seek_delta_sec = delta;
  s_seek_pending = true;
}

uint32_t audio_get_position_ms(void) { return s_position_ms; }

uint32_t audio_get_duration_ms(void) { return s_duration_ms; }

bool audio_get_track_info(audio_track_info_t *out) {
  if (!out)
    return false;

  audio_track_info_t ti = { 0 };
  ti.type = s_track_type;
  if (ti.type == AUDIO_TRACK_TYPE_NONE)
    ti.type = s_track_is_mp3 ? AUDIO_TRACK_TYPE_MP3 : AUDIO_TRACK_TYPE_WAV;

  ti.sample_rate_hz   = s_sample_rate;
  ti.channels         = s_track_channels;
  ti.bits_per_sample  = s_track_bits_per_sample;
  ti.bitrate_kbps     = s_track_bitrate_kbps;
  ti.is_float         = s_track_is_float;
  ti.mp3_vbr          = s_track_mp3_vbr;
  *out = ti;
  return true;
}

void audio_play_file_async(const char *path) {
  if (!path)
    return;

  s_play_task_exited_sem = xSemaphoreCreateBinary();
  audio_stop_playback();

  if (s_play_task) {
    if (s_play_task_exited_sem &&
        xSemaphoreTake(s_play_task_exited_sem, pdMS_TO_TICKS(2000)) == pdTRUE) {
      ESP_LOGI(TAG, "audio_play_file_async: old task exited cleanly");
    } else {
      ESP_LOGW(TAG, "audio_play_file_async: old task did not exit in 2s, forcing");
    }
  }
  if (s_play_task_exited_sem) {
    vSemaphoreDelete(s_play_task_exited_sem);
    s_play_task_exited_sem = NULL;
  }

  size_t len = strlen(path) + 1;
  char *copy = (char *)malloc(len);
  if (!copy)
    return;
  memcpy(copy, path, len);
  audio_start_async(copy);
}

void audio_play_wav_async(const char *path) { audio_play_file_async(path); }
