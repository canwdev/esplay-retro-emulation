#include "audio.h"

#include "platform_log.h"
#include "sim_compat.h"

#include <SDL.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "audio_sim";

#define AUDIO_OUTPUT_QUEUE_TARGET_MS 160
#define AUDIO_OUTPUT_QUEUE_MAX_MS    320
#define AUDIO_WAIT_SLICE_MS          10
#define MP3_IO_BUF_SZ                16384

typedef enum {
  AUDIO_SIM_OK = 0,
  AUDIO_SIM_FAIL = -1,
  AUDIO_SIM_UNSUPPORTED = -2,
  AUDIO_SIM_NOT_FOUND = -3,
} audio_sim_status_t;

typedef struct {
  uint16_t num_channels;
  uint16_t bits_per_sample;
  uint32_t sample_rate;
  uint32_t data_size;
  long data_offset;
  uint16_t audio_format;
  bool is_ieee_float;
} wav_pcm_info_t;

static SDL_mutex *s_state_lock;
static SDL_Thread *s_play_thread;
static SDL_AudioDeviceID s_audio_device;
static SDL_AudioSpec s_device_spec;
static SDL_AudioStream *s_audio_stream;
static uint32_t s_stream_source_rate_hz;

static bool s_stop_requested;
static bool s_playing;
static bool s_paused;
static uint8_t s_volume = 50;
static int32_t s_seek_delta_sec;
static bool s_seek_pending;
static uint32_t s_position_ms;
static uint32_t s_duration_ms;
static uint32_t s_sample_rate;
static bool s_track_is_mp3;
static audio_track_type_t s_track_type;
static uint16_t s_track_channels;
static uint16_t s_track_bits_per_sample;
static uint16_t s_track_bitrate_kbps;
static bool s_track_is_float;
static bool s_track_mp3_vbr;

static int16_t s_stereo_expand[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
static mp3dec_t *s_mp3_dec;
static uint8_t *s_mp3_buf;
static uint32_t s_wav_position_bytes;
static wav_pcm_info_t s_active_wav_info;

static void audio_lock(void) {
  if (s_state_lock)
    SDL_LockMutex(s_state_lock);
}

static void audio_unlock(void) {
  if (s_state_lock)
    SDL_UnlockMutex(s_state_lock);
}

static void audio_reset_track_state(bool is_mp3) {
  audio_lock();
  s_position_ms = 0;
  s_duration_ms = 0;
  s_sample_rate = 0;
  s_track_is_mp3 = is_mp3;
  s_track_type = is_mp3 ? AUDIO_TRACK_TYPE_MP3 : AUDIO_TRACK_TYPE_NONE;
  s_track_channels = 0;
  s_track_bits_per_sample = is_mp3 ? 16 : 0;
  s_track_bitrate_kbps = 0;
  s_track_is_float = false;
  s_track_mp3_vbr = false;
  audio_unlock();
}

static void audio_mark_playing(bool playing) {
  audio_lock();
  s_playing = playing;
  if (!playing) {
    s_paused = false;
    s_seek_pending = false;
    s_seek_delta_sec = 0;
  }
  audio_unlock();
}

static void audio_set_paused_state(bool paused) {
  audio_lock();
  if (s_playing)
    s_paused = paused;
  audio_unlock();
}

static bool audio_should_stop(void) {
  bool stop;
  audio_lock();
  stop = s_stop_requested;
  audio_unlock();
  return stop;
}

static bool audio_is_pause_requested(void) {
  bool paused;
  audio_lock();
  paused = s_paused;
  audio_unlock();
  return paused;
}

static uint8_t audio_volume_snapshot(void) {
  uint8_t volume;
  audio_lock();
  volume = s_volume;
  audio_unlock();
  return volume;
}

static void audio_set_position(uint32_t pos_ms) {
  audio_lock();
  s_position_ms = pos_ms;
  audio_unlock();
}

static void audio_set_duration(uint32_t dur_ms) {
  audio_lock();
  s_duration_ms = dur_ms;
  audio_unlock();
}

static void audio_set_sample_rate(uint32_t sample_rate_hz) {
  audio_lock();
  s_sample_rate = sample_rate_hz;
  audio_unlock();
}

static void audio_update_wav_track_info(const wav_pcm_info_t *info) {
  audio_lock();
  s_track_is_mp3 = false;
  s_track_type = AUDIO_TRACK_TYPE_WAV;
  s_track_channels = info->num_channels;
  s_track_bits_per_sample = info->bits_per_sample;
  s_track_bitrate_kbps = 0;
  s_track_is_float = info->is_ieee_float;
  s_track_mp3_vbr = false;
  s_sample_rate = info->sample_rate;
  audio_unlock();
}

static void audio_update_mp3_frame_info(const mp3dec_frame_info_t *info) {
  if (!info)
    return;

  audio_lock();
  s_track_is_mp3 = true;
  s_track_type = AUDIO_TRACK_TYPE_MP3;
  if (info->channels > 0)
    s_track_channels = (uint16_t)info->channels;
  s_track_bits_per_sample = 16;
  if (info->bitrate_kbps > 0) {
    if (s_track_bitrate_kbps == 0)
      s_track_bitrate_kbps = (uint16_t)info->bitrate_kbps;
    else if (s_track_bitrate_kbps != (uint16_t)info->bitrate_kbps)
      s_track_mp3_vbr = true;
  }
  if (info->hz > 0)
    s_sample_rate = (uint32_t)info->hz;
  audio_unlock();
}

static bool audio_take_seek_request(int32_t *out_delta_sec) {
  bool has_seek = false;
  audio_lock();
  if (s_seek_pending) {
    has_seek = true;
    if (out_delta_sec)
      *out_delta_sec = s_seek_delta_sec;
    s_seek_pending = false;
  }
  audio_unlock();
  return has_seek;
}

static size_t audio_device_bytes_per_second(void) {
  if (s_audio_device == 0 || s_device_spec.channels <= 0 || s_device_spec.freq <= 0)
    return 0;
  size_t sample_bytes = (size_t)(SDL_AUDIO_BITSIZE(s_device_spec.format) / 8);
  return (size_t)s_device_spec.freq * (size_t)s_device_spec.channels * sample_bytes;
}

static size_t audio_queue_target_bytes(void) {
  return audio_device_bytes_per_second() * AUDIO_OUTPUT_QUEUE_TARGET_MS / 1000;
}

static size_t audio_queue_max_bytes(void) {
  return audio_device_bytes_per_second() * AUDIO_OUTPUT_QUEUE_MAX_MS / 1000;
}

static void audio_discard_buffered_output(void) {
  if (s_audio_device != 0) {
    SDL_PauseAudioDevice(s_audio_device, 1);
    SDL_ClearQueuedAudio(s_audio_device);
  }
  if (s_audio_stream)
    SDL_AudioStreamClear(s_audio_stream);
  if (s_audio_device != 0 && !audio_is_pause_requested())
    SDL_PauseAudioDevice(s_audio_device, 0);
}

static bool audio_open_output_if_needed(void) {
  if (s_audio_device != 0)
    return true;

  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 2048;

  s_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &s_device_spec, 0);
  if (s_audio_device == 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_OpenAudioDevice failed: %s",
                 SDL_GetError());
    return false;
  }
  SDL_PauseAudioDevice(s_audio_device, 0);
  return true;
}

static bool audio_stream_setup(uint32_t sample_rate_hz) {
  if (sample_rate_hz < 8000 || sample_rate_hz > 96000) {
    platform_log(PLATFORM_LOG_WARN, TAG, "unsupported sample rate: %lu",
                 (unsigned long)sample_rate_hz);
    return false;
  }
  if (!audio_open_output_if_needed())
    return false;

  if (s_audio_stream && s_stream_source_rate_hz == sample_rate_hz)
    return true;

  audio_discard_buffered_output();
  if (s_audio_stream) {
    SDL_FreeAudioStream(s_audio_stream);
    s_audio_stream = NULL;
  }

  s_audio_stream = SDL_NewAudioStream(AUDIO_S16SYS, 2, (int)sample_rate_hz,
                                      s_device_spec.format, s_device_spec.channels,
                                      s_device_spec.freq);
  if (!s_audio_stream) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_NewAudioStream failed: %s",
                 SDL_GetError());
    return false;
  }

  s_stream_source_rate_hz = sample_rate_hz;
  audio_set_sample_rate(sample_rate_hz);
  if (!audio_is_pause_requested())
    SDL_PauseAudioDevice(s_audio_device, 0);
  return true;
}

static bool audio_pump_stream(void) {
  if (!s_audio_stream || s_audio_device == 0)
    return false;

  uint8_t buffer[4096];
  for (;;) {
    int available = SDL_AudioStreamAvailable(s_audio_stream);
    if (available <= 0)
      break;
    if (available > (int)sizeof(buffer))
      available = (int)sizeof(buffer);
    int got = SDL_AudioStreamGet(s_audio_stream, buffer, available);
    if (got < 0) {
      platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_AudioStreamGet failed: %s",
                   SDL_GetError());
      return false;
    }
    if (got == 0)
      break;
    if (SDL_QueueAudio(s_audio_device, buffer, (uint32_t)got) != 0) {
      platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_QueueAudio failed: %s",
                   SDL_GetError());
      return false;
    }
  }
  return true;
}

static bool audio_wait_while_paused(void) {
  for (;;) {
    bool paused;
    bool stop;
    bool seek_pending;

    audio_lock();
    paused = s_paused;
    stop = s_stop_requested;
    seek_pending = s_seek_pending;
    audio_unlock();

    if (!paused || stop)
      return seek_pending;

    if (s_audio_device != 0)
      SDL_PauseAudioDevice(s_audio_device, 1);

    if (seek_pending)
      return true;

    SDL_Delay(AUDIO_WAIT_SLICE_MS);
  }
}

static bool audio_write_pcm(const int16_t *samples, size_t sample_count) {
  if (!s_audio_stream || s_audio_device == 0 || sample_count == 0)
    return true;

  const size_t max_bytes = audio_queue_max_bytes();
  const size_t target_bytes = audio_queue_target_bytes();
  const int source_bytes = (int)(sample_count * sizeof(int16_t));

  while (!audio_should_stop() &&
         SDL_GetQueuedAudioSize(s_audio_device) > max_bytes) {
    if (audio_wait_while_paused())
      return true;
    SDL_Delay(AUDIO_WAIT_SLICE_MS);
  }
  if (audio_should_stop())
    return true;

  if (SDL_AudioStreamPut(s_audio_stream, samples, source_bytes) < 0) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_AudioStreamPut failed: %s",
                 SDL_GetError());
    return false;
  }
  if (!audio_pump_stream())
    return false;

  if (!audio_is_pause_requested())
    SDL_PauseAudioDevice(s_audio_device, 0);

  while (!audio_should_stop() &&
         SDL_GetQueuedAudioSize(s_audio_device) > target_bytes) {
    if (audio_wait_while_paused())
      return true;
    SDL_Delay(AUDIO_WAIT_SLICE_MS);
  }

  return true;
}

static void audio_flush_and_drain_output(void) {
  if (!s_audio_stream || s_audio_device == 0)
    return;

  SDL_AudioStreamFlush(s_audio_stream);
  audio_pump_stream();

  while (!audio_should_stop()) {
    if (audio_wait_while_paused())
      continue;
    if (SDL_GetQueuedAudioSize(s_audio_device) == 0)
      break;
    SDL_Delay(AUDIO_WAIT_SLICE_MS);
  }
}

static int16_t apply_volume_i16(int16_t sample, uint8_t volume) {
  int32_t v = (int32_t)volume;
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
  if (scaled < -32768)
    scaled = -32768;
  return (int16_t)scaled;
}

static int16_t float_sample_to_i16(float x, uint8_t volume) {
  if (!isfinite(x))
    x = 0.0f;
  if (x > 1.0f)
    x = 1.0f;
  else if (x < -1.0f)
    x = -1.0f;
  return apply_volume_i16((int16_t)(x * 32767.0f), volume);
}

static int audio_path_casecmp(const char *a, const char *b) {
#ifdef _WIN32
  return _stricmp(a, b);
#else
  return strcasecmp(a, b);
#endif
}

static bool path_has_ext(const char *path, const char *ext) {
  const char *dot = strrchr(path, '.');
  return dot != NULL && audio_path_casecmp(dot, ext) == 0;
}

static bool path_is_mp3(const char *path) {
  return path_has_ext(path, ".mp3");
}

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

#define WAV_FORMAT_PCM            0x0001u
#define WAV_FORMAT_IEEE_FLOAT     0x0003u
#define WAV_FORMAT_EXTENSIBLE     0xFFFEu
#define WAV_SUBFORMAT_IEEE_FLOAT  0x00000003u

static audio_sim_status_t wav_load_pcm_info(FILE *f, wav_pcm_info_t *out) {
  uint8_t riff[12];
  if (fread(riff, 1, sizeof(riff), f) != sizeof(riff))
    return AUDIO_SIM_FAIL;
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
    return AUDIO_SIM_UNSUPPORTED;

  memset(out, 0, sizeof(*out));

  for (;;) {
    uint8_t cid[4];
    uint8_t szraw[4];
    if (fread(cid, 1, 4, f) != 4 || fread(szraw, 1, 4, f) != 4)
      return AUDIO_SIM_FAIL;

    uint32_t chunk_size = read_u32_le(szraw);
    if (memcmp(cid, "fmt ", 4) == 0) {
      uint8_t fmt_hdr[40];
      if (chunk_size < 16)
        return AUDIO_SIM_UNSUPPORTED;
      size_t read_fmt = chunk_size > sizeof(fmt_hdr) ? sizeof(fmt_hdr) : chunk_size;
      if (fread(fmt_hdr, 1, read_fmt, f) != read_fmt)
        return AUDIO_SIM_FAIL;
      if (chunk_size > read_fmt &&
          fseek(f, (long)(chunk_size - read_fmt), SEEK_CUR) != 0)
        return AUDIO_SIM_FAIL;

      out->audio_format = read_u16_le(fmt_hdr);
      out->num_channels = read_u16_le(fmt_hdr + 2);
      out->sample_rate = read_u32_le(fmt_hdr + 4);
      out->bits_per_sample = read_u16_le(fmt_hdr + 14);

      bool ext_float = false;
      if (out->audio_format == WAV_FORMAT_EXTENSIBLE && chunk_size >= 40)
        ext_float = read_u32_le(fmt_hdr + 24) == WAV_SUBFORMAT_IEEE_FLOAT;

      out->is_ieee_float =
          (out->audio_format == WAV_FORMAT_IEEE_FLOAT) || ext_float;

      bool valid =
          (out->audio_format == WAV_FORMAT_PCM) || out->is_ieee_float ||
          (out->audio_format == WAV_FORMAT_EXTENSIBLE &&
           out->bits_per_sample == 16);
      if (!valid)
        return AUDIO_SIM_UNSUPPORTED;
    } else if (memcmp(cid, "data", 4) == 0) {
      out->data_offset = ftell(f);
      out->data_size = chunk_size;
      if (fseek(f, (long)chunk_size, SEEK_CUR) != 0)
        return AUDIO_SIM_FAIL;
      break;
    } else if (fseek(f, (long)chunk_size, SEEK_CUR) != 0) {
      return AUDIO_SIM_FAIL;
    }

    if ((chunk_size & 1u) != 0 && fseek(f, 1, SEEK_CUR) != 0)
      return AUDIO_SIM_FAIL;
  }

  if (!out->data_offset || !out->sample_rate)
    return AUDIO_SIM_UNSUPPORTED;
  if (out->num_channels != 1 && out->num_channels != 2)
    return AUDIO_SIM_UNSUPPORTED;
  if (out->is_ieee_float) {
    if (out->bits_per_sample != 32)
      return AUDIO_SIM_UNSUPPORTED;
  } else if (out->bits_per_sample != 16) {
    return AUDIO_SIM_UNSUPPORTED;
  }

  return AUDIO_SIM_OK;
}

static size_t wav_frame_bytes(const wav_pcm_info_t *info) {
  return (size_t)info->num_channels * (info->is_ieee_float ? 4u : 2u);
}

static uint32_t wav_bytes_to_ms(const wav_pcm_info_t *info, uint32_t bytes) {
  size_t frame_bytes = wav_frame_bytes(info);
  if (!info->sample_rate || frame_bytes == 0)
    return 0;
  return (uint32_t)((bytes / frame_bytes) * 1000ULL / info->sample_rate);
}

static bool mp3_hz_supported(int hz) {
  return hz == 8000 || hz == 11025 || hz == 12000 || hz == 16000 ||
         hz == 22050 || hz == 24000 || hz == 32000 || hz == 44100 ||
         hz == 48000;
}

static bool mp3_frame_info_valid(const mp3dec_frame_info_t *info, int samples) {
  if (!info || samples <= 0 || info->frame_bytes <= 0)
    return false;
  if (info->channels != 1 && info->channels != 2)
    return false;
  if (!mp3_hz_supported(info->hz))
    return false;
  if (info->bitrate_kbps <= 0 || info->bitrate_kbps > 320)
    return false;
  return true;
}

static bool audio_write_stereo_from_mono(const int16_t *mono, size_t frames) {
  if (frames > MINIMP3_MAX_SAMPLES_PER_FRAME)
    frames = MINIMP3_MAX_SAMPLES_PER_FRAME;

  uint8_t volume = audio_volume_snapshot();
  for (size_t i = frames; i > 0; i--) {
    int16_t sample = apply_volume_i16(mono[i - 1], volume);
    s_stereo_expand[(i - 1) * 2] = sample;
    s_stereo_expand[(i - 1) * 2 + 1] = sample;
  }
  return audio_write_pcm(s_stereo_expand, frames * 2);
}

static bool audio_write_stereo_interleaved(int16_t *stereo, size_t samples) {
  if (samples > MINIMP3_MAX_SAMPLES_PER_FRAME * 2)
    samples = MINIMP3_MAX_SAMPLES_PER_FRAME * 2;

  uint8_t volume = audio_volume_snapshot();
  for (size_t i = 0; i < samples; i++)
    stereo[i] = apply_volume_i16(stereo[i], volume);
  return audio_write_pcm(stereo, samples);
}

static bool wav_apply_seek(FILE *f, const wav_pcm_info_t *info, int32_t delta_sec) {
  if (!delta_sec)
    return true;

  size_t frame_bytes = wav_frame_bytes(info);
  int64_t delta_bytes =
      (int64_t)delta_sec * (int64_t)info->sample_rate * (int64_t)frame_bytes;
  int64_t new_pos = (int64_t)s_wav_position_bytes + delta_bytes;
  if (new_pos < 0)
    new_pos = 0;
  if ((uint64_t)new_pos > info->data_size)
    new_pos = info->data_size;
  new_pos -= new_pos % (int64_t)frame_bytes;

  s_wav_position_bytes = (uint32_t)new_pos;
  audio_set_position(wav_bytes_to_ms(info, s_wav_position_bytes));
  return fseek(f, info->data_offset + (long)s_wav_position_bytes, SEEK_SET) == 0;
}

static audio_sim_status_t stream_wav_body(FILE *f, const wav_pcm_info_t *info) {
  if (fseek(f, info->data_offset + (long)s_wav_position_bytes, SEEK_SET) != 0)
    return AUDIO_SIM_FAIL;

  uint32_t remaining = info->data_size - s_wav_position_bytes;
  uint8_t raw[4096];
  const size_t frame_bytes = wav_frame_bytes(info);

  while (remaining > 0 && !audio_should_stop()) {
    if (audio_wait_while_paused()) {
      int32_t delta_sec = 0;
      if (audio_take_seek_request(&delta_sec)) {
        audio_discard_buffered_output();
        wav_apply_seek(f, info, delta_sec);
        remaining = info->data_size - s_wav_position_bytes;
      }
      continue;
    }

    size_t want = remaining > sizeof(raw) ? sizeof(raw) : remaining;
    size_t rd = fread(raw, 1, want, f);
    if (rd == 0)
      break;

    bool ok = true;
    if (info->is_ieee_float) {
      size_t frames = rd / frame_bytes;
      uint8_t volume = audio_volume_snapshot();
      if (info->num_channels == 1) {
        for (size_t i = 0; i < frames; i++) {
          float sample_f = 0.0f;
          memcpy(&sample_f, raw + i * sizeof(float), sizeof(float));
          int16_t sample = float_sample_to_i16(sample_f, volume);
          s_stereo_expand[i * 2] = sample;
          s_stereo_expand[i * 2 + 1] = sample;
        }
        ok = audio_write_pcm(s_stereo_expand, frames * 2);
      } else {
        for (size_t i = 0; i < frames; i++) {
          float fl = 0.0f;
          float fr = 0.0f;
          memcpy(&fl, raw + i * 8, sizeof(float));
          memcpy(&fr, raw + i * 8 + sizeof(float), sizeof(float));
          s_stereo_expand[i * 2] = float_sample_to_i16(fl, volume);
          s_stereo_expand[i * 2 + 1] = float_sample_to_i16(fr, volume);
        }
        ok = audio_write_pcm(s_stereo_expand, frames * 2);
      }
    } else if (info->num_channels == 1) {
      ok = audio_write_stereo_from_mono((const int16_t *)raw, rd / 2);
    } else {
      memcpy(s_stereo_expand, raw, rd);
      ok = audio_write_stereo_interleaved(s_stereo_expand, rd / sizeof(int16_t));
    }

    if (!ok)
      return AUDIO_SIM_FAIL;

    s_wav_position_bytes += (uint32_t)rd;
    audio_set_position(wav_bytes_to_ms(info, s_wav_position_bytes));
    remaining -= (uint32_t)rd;
  }

  return AUDIO_SIM_OK;
}

static uint32_t mp3_samples_to_ms(uint64_t samples, uint32_t hz) {
  if (!hz)
    return 0;
  return (uint32_t)(samples * 1000ULL / hz);
}

static audio_sim_status_t mp3_skip_to_ms(FILE *f, mp3dec_t *dec,
                                         uint32_t target_ms, uint8_t *buf,
                                         size_t buf_sz, size_t *buf_len,
                                         uint64_t *decoded_samples) {
  if (fseek(f, 0, SEEK_SET) != 0)
    return AUDIO_SIM_FAIL;

  *buf_len = 0;
  *decoded_samples = 0;
  mp3dec_init(dec);

  mp3dec_frame_info_t info;
  while (!audio_should_stop() && s_position_ms < target_ms) {
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
        if (info.hz > 0) {
          audio_set_sample_rate((uint32_t)info.hz);
        }
        *decoded_samples += (uint64_t)samples;
        audio_set_position(mp3_samples_to_ms(*decoded_samples, s_sample_rate));
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
  return AUDIO_SIM_OK;
}

static audio_sim_status_t mp3_fast_seek(FILE *f, mp3dec_t *dec, uint32_t target_ms,
                                        long file_size, size_t cur_buf_len,
                                        size_t *out_buf_len,
                                        uint64_t *decoded_samples) {
  if (s_sample_rate == 0 || file_size <= 0)
    return AUDIO_SIM_UNSUPPORTED;

  uint32_t est_duration = s_duration_ms;
  if (est_duration == 0) {
    if (s_position_ms == 0)
      return AUDIO_SIM_UNSUPPORTED;
    long read_head = ftell(f);
    if (read_head < 0)
      return AUDIO_SIM_FAIL;
    long decoded_byte = read_head - (long)cur_buf_len;
    if (decoded_byte <= 0)
      return AUDIO_SIM_UNSUPPORTED;
    est_duration = (uint32_t)((uint64_t)(unsigned long)file_size *
                              (uint64_t)s_position_ms / (uint64_t)decoded_byte);
    if (est_duration == 0)
      return AUDIO_SIM_UNSUPPORTED;
  }

  long byte_pos = (long)((uint64_t)target_ms * (uint64_t)(unsigned long)file_size /
                         (uint64_t)est_duration);
  if (byte_pos < 0)
    byte_pos = 0;
  if (byte_pos > file_size - 4)
    byte_pos = file_size > 4 ? file_size - 4 : 0;
  if (fseek(f, byte_pos, SEEK_SET) != 0)
    return AUDIO_SIM_FAIL;

  uint8_t scan[256];
  size_t scanned = 0;
  while (scanned < 4096) {
    size_t n = fread(scan, 1, sizeof(scan), f);
    if (n < 2)
      break;
    for (size_t i = 0; i + 1 < n; i++) {
      if (scan[i] == 0xFF && (scan[i + 1] & 0xE0) == 0xE0) {
        long sync_pos = byte_pos + (long)(scanned + i);
        if (fseek(f, sync_pos, SEEK_SET) != 0)
          return AUDIO_SIM_FAIL;
        mp3dec_init(dec);
        *out_buf_len = 0;
        *decoded_samples =
            (uint64_t)target_ms * (uint64_t)s_sample_rate / 1000ULL;
        audio_set_position(target_ms);
        return AUDIO_SIM_OK;
      }
    }
    scanned += n;
  }

  return AUDIO_SIM_NOT_FOUND;
}

static audio_sim_status_t stream_mp3_body(FILE *f, long file_size) {
  mp3dec_init(s_mp3_dec);

  size_t buf_len = 0;
  uint64_t decoded_samples = 0;
  bool rate_set = false;
  bool got_audio_frame = false;
  bool eof_reached = false;

  if (fseek(f, 0, SEEK_SET) != 0)
    return AUDIO_SIM_FAIL;

  while (!audio_should_stop()) {
    if (audio_wait_while_paused()) {
      int32_t delta_sec = 0;
      if (audio_take_seek_request(&delta_sec)) {
        int64_t target = (int64_t)s_position_ms + (int64_t)delta_sec * 1000;
        if (target < 0)
          target = 0;
        if ((uint32_t)target > s_duration_ms)
          target = s_duration_ms;

        eof_reached = false;
        audio_discard_buffered_output();
        if (mp3_fast_seek(f, s_mp3_dec, (uint32_t)target, file_size, buf_len,
                          &buf_len, &decoded_samples) != AUDIO_SIM_OK) {
          mp3_skip_to_ms(f, s_mp3_dec, (uint32_t)target, s_mp3_buf,
                         MP3_IO_BUF_SZ, &buf_len, &decoded_samples);
        }
      }
      continue;
    }

    if (!eof_reached && buf_len < MP3_IO_BUF_SZ) {
      size_t n = fread(s_mp3_buf + buf_len, 1, MP3_IO_BUF_SZ - buf_len, f);
      if (n == 0)
        eof_reached = true;
      else
        buf_len += n;
    }
    if (buf_len == 0)
      break;

    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(s_mp3_dec, s_mp3_buf, (int)buf_len,
                                      s_stereo_expand, &info);
    if (info.frame_bytes <= 0) {
      if (buf_len >= 1024) {
        memmove(s_mp3_buf, s_mp3_buf + 1, buf_len - 1);
        buf_len -= 1;
      } else if (eof_reached) {
        break;
      }
      continue;
    }

    memmove(s_mp3_buf, s_mp3_buf + info.frame_bytes,
            buf_len - (size_t)info.frame_bytes);
    buf_len -= (size_t)info.frame_bytes;

    if (!mp3_frame_info_valid(&info, samples))
      continue;

    got_audio_frame = true;
    audio_update_mp3_frame_info(&info);

    if (!rate_set) {
      if (!audio_stream_setup((uint32_t)info.hz))
        return AUDIO_SIM_FAIL;
      rate_set = true;
      if (file_size > 0 && info.bitrate_kbps > 0) {
        audio_set_duration((uint32_t)((uint64_t)(unsigned long)file_size * 8ULL /
                                      (uint64_t)info.bitrate_kbps));
      }
    } else if ((uint32_t)info.hz != s_sample_rate) {
      continue;
    }

    bool ok = (info.channels == 1)
                  ? audio_write_stereo_from_mono(s_stereo_expand, (size_t)samples)
                  : audio_write_stereo_interleaved(s_stereo_expand,
                                                   (size_t)samples * 2);
    if (!ok)
      return AUDIO_SIM_FAIL;

    decoded_samples += (uint64_t)samples;
    audio_set_position(mp3_samples_to_ms(decoded_samples, s_sample_rate));
  }

  if (!got_audio_frame) {
    platform_log(PLATFORM_LOG_WARN, TAG, "invalid MP3 stream");
    return AUDIO_SIM_UNSUPPORTED;
  }

  return AUDIO_SIM_OK;
}

static audio_sim_status_t play_wav_file(FILE *f) {
  wav_pcm_info_t info;
  audio_sim_status_t status = wav_load_pcm_info(f, &info);
  if (status != AUDIO_SIM_OK)
    return status;

  if (!audio_stream_setup(info.sample_rate))
    return AUDIO_SIM_FAIL;

  s_active_wav_info = info;
  s_wav_position_bytes = 0;
  audio_update_wav_track_info(&info);
  audio_set_duration(wav_bytes_to_ms(&info, info.data_size));
  audio_set_position(0);

  return stream_wav_body(f, &info);
}

static audio_sim_status_t play_mp3_file(FILE *f) {
  if (fseek(f, 0, SEEK_END) != 0)
    return AUDIO_SIM_FAIL;
  long file_size = ftell(f);
  if (file_size <= 0)
    return AUDIO_SIM_FAIL;
  if (fseek(f, 0, SEEK_SET) != 0)
    return AUDIO_SIM_FAIL;

  audio_reset_track_state(true);
  return stream_mp3_body(f, file_size);
}

static int play_thread_main(void *arg) {
  char *path = (char *)arg;
  unsigned long winerr = 0;

  audio_reset_track_state(false);
  audio_mark_playing(true);
  audio_lock();
  s_stop_requested = false;
  audio_unlock();

  FILE *f = sim_fopen_utf8(path, "rb", &winerr);
  if (!f) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "open failed: %s errno=%d winerr=%lu",
                 path, errno, winerr);
    goto done;
  }

  if (path_is_mp3(path)) {
    if (!s_mp3_dec || !s_mp3_buf) {
      platform_log(PLATFORM_LOG_ERROR, TAG, "MP3 buffers not initialized");
      fclose(f);
      goto done;
    }
    if (play_mp3_file(f) == AUDIO_SIM_UNSUPPORTED)
      platform_log(PLATFORM_LOG_WARN, TAG, "unsupported MP3 stream: %s", path);
  } else {
    if (play_wav_file(f) == AUDIO_SIM_UNSUPPORTED)
      platform_log(PLATFORM_LOG_WARN, TAG, "unsupported WAV stream: %s", path);
  }

  fclose(f);
  if (!audio_should_stop())
    audio_flush_and_drain_output();

done:
  free(path);
  if (audio_should_stop())
    audio_discard_buffered_output();
  audio_mark_playing(false);
  audio_lock();
  s_play_thread = NULL;
  audio_unlock();
  return 0;
}

void audio_init(void) {
  if (!s_state_lock) {
    s_state_lock = SDL_CreateMutex();
    if (!s_state_lock) {
      platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_CreateMutex failed: %s",
                   SDL_GetError());
      return;
    }
  }

  if (!s_mp3_dec)
    s_mp3_dec = (mp3dec_t *)calloc(1, sizeof(mp3dec_t));
  if (!s_mp3_buf)
    s_mp3_buf = (uint8_t *)malloc(MP3_IO_BUF_SZ);
}

void audio_play_file_async(const char *path) {
  if (!path)
    return;

  audio_stop_playback();

  SDL_Thread *old_thread = NULL;
  audio_lock();
  old_thread = s_play_thread;
  audio_unlock();
  if (old_thread)
    SDL_WaitThread(old_thread, NULL);

  size_t len = strlen(path) + 1;
  char *path_copy = (char *)malloc(len);
  if (!path_copy)
    return;
  memcpy(path_copy, path, len);

  audio_lock();
  s_stop_requested = false;
  s_paused = false;
  s_seek_pending = false;
  s_seek_delta_sec = 0;
  audio_unlock();

  SDL_Thread *thread =
      SDL_CreateThread(play_thread_main, "audio_play", path_copy);
  if (!thread) {
    platform_log(PLATFORM_LOG_ERROR, TAG, "SDL_CreateThread failed: %s",
                 SDL_GetError());
    free(path_copy);
    return;
  }

  audio_lock();
  s_play_thread = thread;
  audio_unlock();
}

void audio_play_wav_async(const char *path) {
  audio_play_file_async(path);
}

void audio_stop_playback(void) {
  audio_lock();
  s_stop_requested = true;
  s_paused = false;
  audio_unlock();

  if (s_audio_device != 0) {
    SDL_PauseAudioDevice(s_audio_device, 0);
    audio_discard_buffered_output();
  }
}

bool audio_is_playing(void) {
  bool playing;
  audio_lock();
  playing = s_playing;
  audio_unlock();
  return playing;
}

void audio_set_volume(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  audio_lock();
  s_volume = pct;
  audio_unlock();
}

uint8_t audio_get_volume(void) {
  uint8_t volume;
  audio_lock();
  volume = s_volume;
  audio_unlock();
  return volume;
}

void audio_pause(void) {
  if (!audio_is_playing())
    return;
  audio_set_paused_state(true);
  if (s_audio_device != 0)
    SDL_PauseAudioDevice(s_audio_device, 1);
}

void audio_resume(void) {
  if (!audio_is_playing())
    return;
  audio_set_paused_state(false);
  if (s_audio_device != 0)
    SDL_PauseAudioDevice(s_audio_device, 0);
}

void audio_toggle_pause(void) {
  if (!audio_is_playing())
    return;
  if (audio_is_paused())
    audio_resume();
  else
    audio_pause();
}

bool audio_is_paused(void) {
  bool paused;
  audio_lock();
  paused = s_paused;
  audio_unlock();
  return paused;
}

void audio_seek_seconds(int delta) {
  if (!audio_is_playing())
    return;
  audio_lock();
  s_seek_delta_sec = delta;
  s_seek_pending = true;
  audio_unlock();
}

uint32_t audio_get_position_ms(void) {
  uint32_t pos;
  audio_lock();
  pos = s_position_ms;
  audio_unlock();
  return pos;
}

uint32_t audio_get_duration_ms(void) {
  uint32_t dur;
  audio_lock();
  dur = s_duration_ms;
  audio_unlock();
  return dur;
}

bool audio_get_track_info(audio_track_info_t *out) {
  if (!out)
    return false;

  audio_lock();
  out->type = s_track_type;
  if (out->type == AUDIO_TRACK_TYPE_NONE)
    out->type = s_track_is_mp3 ? AUDIO_TRACK_TYPE_MP3 : AUDIO_TRACK_TYPE_WAV;
  out->sample_rate_hz = s_sample_rate;
  out->channels = s_track_channels;
  out->bits_per_sample = s_track_bits_per_sample;
  out->bitrate_kbps = s_track_bitrate_kbps;
  out->is_float = s_track_is_float;
  out->mp3_vbr = s_track_mp3_vbr;
  audio_unlock();
  return true;
}
