#include "preview_audio.h"

#include "audio.h"
#include "eq.h"
#include "id3.h"
#include "file_manager.h"
#include "hal_settings.h"
#include "input_bridge.h"
#include "input_repeat.h"
#include "platform_log.h"
#include "platform_mem.h"
#include "platform_time.h"
#include "ui_backlight.h"
#include "ui_chrome.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_theme.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#else
#include "esp_random.h"
#endif

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "preview_audio";

#define PREVIEW_AUDIO_LOGI(...) platform_log(PLATFORM_LOG_INFO, TAG, __VA_ARGS__)
#define PREVIEW_AUDIO_LOGW(...) platform_log(PLATFORM_LOG_WARN, TAG, __VA_ARGS__)
#define PREVIEW_AUDIO_LOGE(...) platform_log(PLATFORM_LOG_ERROR, TAG, __VA_ARGS__)

static char *preview_audio_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *d = malloc(len + 1);
  if (d)
    memcpy(d, s, len + 1);
  return d;
}

static uint32_t preview_audio_random_u32(void) {
  #ifdef TARGET_SIM
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
  #else
    return esp_random();
  #endif
}

static void preview_audio_wait_ms(uint32_t ms) {
  platform_sleep_ms(ms);
}

#define AUDIO_PLAYLIST_MAX 512
#define AUDIO_PATH_MAX     256
#define AUDIO_SEEK_STEP_SECONDS   5
#define AUDIO_ADJUST_ACCEL_EVERY  4
#define AUDIO_ADJUST_MAX_SCALE    4

typedef enum {
  PLAY_MODE_LIST_LOOP = 0, /* wrap around at end of list  (default) */
  PLAY_MODE_SHUFFLE,       /* Fisher-Yates shuffled cycle, no repeat per round */
  PLAY_MODE_SINGLE_LOOP,   /* replay the same track forever          */
  PLAY_MODE_LIST_PLAY,     /* play to last track then stop           */
  PLAY_MODE_COUNT,
} play_mode_t;

static char **s_playlist = NULL;
static int  s_playlist_count;
static int  s_current_index;
static int  s_shuffle_order[AUDIO_PLAYLIST_MAX];
static int  s_shuffle_pos;
static char s_current_path[AUDIO_PATH_MAX];
static char s_playlist_cwd[AUDIO_PATH_MAX];
static bool s_playlist_from_shared;
static bool s_active;
static bool s_session_active;
static bool s_close_to_background;
static lv_timer_t *s_session_timer;

static ui_chrome_t s_chrome;
static lv_obj_t *s_tag_title_label;
static lv_obj_t *s_tag_artist_label;
static lv_obj_t *s_tech_label;
static lv_obj_t *s_track_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_vol_bar;
static lv_obj_t *s_vol_pct_label;
static lv_obj_t *s_eq_label;
static lv_obj_t *s_mode_label;
static uint32_t  s_last_ui_ms;
static uint8_t   s_last_vol;
static int       s_last_progress;
static uint32_t  s_last_pos_sec;
static uint32_t  s_last_dur_sec;
static bool      s_last_paused;
static bool      s_last_playing;
static int       s_last_track_index;
static int       s_last_track_count;
static audio_track_type_t s_last_track_type;
static uint32_t  s_last_sample_rate_hz;
static uint16_t  s_last_channels;
static uint16_t  s_last_bits_per_sample;
static uint16_t  s_last_bitrate_kbps;
static bool      s_last_is_float;
static bool      s_last_mp3_vbr;
static play_mode_t s_play_mode;
/* Set true once the current track is confirmed playing; cleared on track end
 * or when a new track is started, to gate auto-advance triggering. */
static bool s_track_confirmed_playing;
static input_repeat_state_t s_volume_repeat;
static input_repeat_state_t s_seek_repeat;
static const char *preview_audio_current_path(void);
static void preview_audio_close_foreground(void);
static void preview_audio_reset_session_state(void);
static void preview_audio_stop_session_internal(void);
static void preview_audio_session_tick(bool force_ui);
static bool preview_audio_build_foreground(lv_obj_t *screen);
static void preview_audio_session_timer_cb(lv_timer_t *timer);
static bool preview_audio_prepare_session(const char *path, char *shared_names,
                                          int shared_count, int shared_index,
                                          int shared_name_stride,
                                          const char *cwd_override);
static void preview_audio_persist_state(bool armed);
static bool preview_audio_path_exists(const char *path);
static bool preview_audio_copy_dirname(const char *path, char *out,
                                       size_t out_sz);

static const input_repeat_config_t s_audio_adjust_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = AUDIO_ADJUST_ACCEL_EVERY,
    .max_scale = AUDIO_ADJUST_MAX_SCALE,
};

/* ------------------------------------------------------------------ helpers */

static const char *play_mode_text(play_mode_t m) {
  switch (m) {
    case PLAY_MODE_LIST_LOOP:   return LV_SYMBOL_LOOP " All Loop";
    case PLAY_MODE_SHUFFLE:     return LV_SYMBOL_SHUFFLE " Random";
    case PLAY_MODE_SINGLE_LOOP: return LV_SYMBOL_LOOP " x1 Loop";
    case PLAY_MODE_LIST_PLAY:   return LV_SYMBOL_NEXT " Sequential";
    default: return "";
  }
}

static void update_mode_label(void) {
  if (!s_mode_label || !lv_obj_is_valid(s_mode_label))
    return;
  lv_label_set_text(s_mode_label, play_mode_text(s_play_mode));
}

static void preview_audio_ensure_session_timer(void) {
  if (s_session_timer)
    return;
  s_session_timer =
      lv_timer_create(preview_audio_session_timer_cb, 100, NULL);
  if (s_session_timer)
    lv_timer_set_repeat_count(s_session_timer, -1);
}

static void preview_audio_drop_session_timer(void) {
  if (!s_session_timer)
    return;
  lv_timer_delete(s_session_timer);
  s_session_timer = NULL;
}

static bool preview_audio_path_exists(const char *path) {
  if (!path || path[0] == '\0')
    return false;
#ifdef TARGET_SIM
  bool is_dir = false;
  bool is_reg = false;
  unsigned long size = 0;
  unsigned long winerr = 0;
  return sim_path_get_info_utf8(path, &is_dir, &is_reg, &size, &winerr) && is_reg;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static bool preview_audio_copy_dirname(const char *path, char *out,
                                       size_t out_sz) {
  if (!path || !out || out_sz == 0)
    return false;
  const char *slash = strrchr(path, '/');
  if (!slash)
    return false;
  size_t len = (size_t)(slash - path);
  if (len == 0 || len >= out_sz)
    return false;
  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

static void preview_audio_persist_state(bool armed) {
  hal_settings_save(SettingMusicSessionArmed, armed ? 1 : 0);
  if (s_current_path[0] != '\0')
    hal_settings_save_str(SettingMusicSessionPath, s_current_path);
}

static void preview_audio_sync_file_manager_cwd(void) {
  if (s_playlist_cwd[0] != '\0')
    fm_set_cwd(s_playlist_cwd);
}

/* ------------------------------------------------------------------ can_open */

static bool preview_audio_can_open(const char *path) {
  return fm_is_playable_audio_filename(fm_base_name(path));
}

/* ------------------------------------------------------------------ playlist */

static int preview_playlist_compare(const void *a, const void *b) {
  return strcasecmp(*(const char * const *)a, *(const char * const *)b);
}

static void preview_audio_free_playlist(void) {
  if (s_playlist) {
    for (int i = 0; i < s_playlist_count; i++) {
      if (s_playlist[i]) {
        free(s_playlist[i]);
      }
    }
    free(s_playlist);
    s_playlist = NULL;
  }
}

static void preview_audio_shuffle_reset(int start_index) {
  if (s_playlist_count <= 0)
    return;

  if (start_index < 0 || start_index >= s_playlist_count)
    start_index = 0;

  for (int i = 0; i < s_playlist_count; i++)
    s_shuffle_order[i] = i;

  if (start_index != 0) {
    int tmp = s_shuffle_order[0];
    s_shuffle_order[0] = s_shuffle_order[start_index];
    s_shuffle_order[start_index] = tmp;
  }

  for (int i = s_playlist_count - 1; i > 1; i--) {
    uint32_t r = preview_audio_random_u32();
    int j = 1 + (int)(r % (uint32_t)i);
    int tmp = s_shuffle_order[i];
    s_shuffle_order[i] = s_shuffle_order[j];
    s_shuffle_order[j] = tmp;
  }

  s_shuffle_pos = 0;
}

static int preview_audio_shuffle_find_pos(int playlist_index) {
  for (int i = 0; i < s_playlist_count; i++) {
    if (s_shuffle_order[i] == playlist_index)
      return i;
  }
  return 0;
}

static int preview_audio_shuffle_step(int delta, bool restart_round) {
  if (s_playlist_count <= 1)
    return s_current_index;

  s_shuffle_pos = preview_audio_shuffle_find_pos(s_current_index);

  if (delta > 0) {
    if (restart_round && s_shuffle_pos >= s_playlist_count - 1) {
      preview_audio_shuffle_reset(s_current_index);
      if (s_playlist_count > 1)
        s_shuffle_pos = 1;
      return s_shuffle_order[s_shuffle_pos];
    }
    s_shuffle_pos++;
    if (s_shuffle_pos >= s_playlist_count)
      s_shuffle_pos = 0;
  } else if (delta < 0) {
    s_shuffle_pos--;
    if (s_shuffle_pos < 0)
      s_shuffle_pos = s_playlist_count - 1;
  }

  return s_shuffle_order[s_shuffle_pos];
}

static bool preview_audio_build_playlist(preview_open_args_t *args,
                                         const char *current) {
  s_playlist_count = 0;
  s_current_index  = 0;
  s_shuffle_pos    = 0;

  if (args->cwd)
    strlcpy(s_playlist_cwd, args->cwd, sizeof(s_playlist_cwd));
  else
    preview_audio_copy_dirname(current, s_playlist_cwd, sizeof(s_playlist_cwd));

  if (args->shared_names && args->shared_count > 0 && args->shared_name_stride == FM_NAME_LEN) {
    s_playlist = malloc((size_t)args->shared_count * sizeof(char *));
    if (!s_playlist) {
      PREVIEW_AUDIO_LOGE("Failed to allocate memory for s_playlist from shared");
      platform_free((void *)args->shared_names);
      args->shared_names = NULL;
      return false;
    }
    for (int i = 0; i < args->shared_count; i++) {
      const char *src = args->shared_names + (size_t)i * FM_NAME_LEN;
      s_playlist[i] = preview_audio_strdup(src);
    }
    s_playlist_count = args->shared_count;
    platform_free((void *)args->shared_names);
    args->shared_names = NULL;
    s_playlist_from_shared = true;
    s_current_index = args->shared_index >= 0 ? args->shared_index : 0;
    if (s_current_index >= s_playlist_count)
      s_current_index = 0;
    preview_audio_shuffle_reset(s_current_index);
    return true;
  }

  s_playlist = malloc(AUDIO_PLAYLIST_MAX * sizeof(char *));
  if (!s_playlist) {
    PREVIEW_AUDIO_LOGE("Failed to allocate memory for s_playlist");
    return false;
  }

  DIR *dir = opendir(s_playlist_cwd);
  if (!dir) {
    free(s_playlist);
    s_playlist = NULL;
    return false;
  }

  struct dirent *de;
  while ((de = readdir(dir)) != NULL && s_playlist_count < AUDIO_PLAYLIST_MAX) {
    if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN)
      continue;
    if (!fm_is_playable_audio_filename(de->d_name))
      continue;
    s_playlist[s_playlist_count] = preview_audio_strdup(de->d_name);
    if (s_playlist[s_playlist_count]) {
      s_playlist_count++;
    }
  }
  closedir(dir);

  if (s_playlist_count > 1)
    qsort(s_playlist, s_playlist_count, sizeof(char *), preview_playlist_compare);

  const char *cur = fm_base_name(current);
  for (int i = 0; i < s_playlist_count; i++) {
    if (strcasecmp(s_playlist[i], cur) == 0) {
      s_current_index = i;
      break;
    }
  }

  preview_audio_shuffle_reset(s_current_index);
  return true;
}

/* ------------------------------------------------------------------ UI update */

static void preview_audio_update_ui(bool force) {
  if (!s_active)
    return;
  /* No point updating widgets the user cannot see. */
  if (!ui_backlight_is_on())
    return;
  if (!s_progress_bar || !s_vol_bar || !s_time_label || !s_status_label)
    return;
  if (!lv_obj_is_valid(s_progress_bar) || !lv_obj_is_valid(s_vol_bar) ||
      !lv_obj_is_valid(s_time_label) || !lv_obj_is_valid(s_status_label))
    return;
  if (!force) {
    uint32_t now = lv_tick_get();
    if ((now - s_last_ui_ms) < 250)
      return;
    s_last_ui_ms = now;
  } else {
    s_last_ui_ms = lv_tick_get();
  }

  uint32_t pos = audio_get_position_ms();
  uint32_t dur = audio_get_duration_ms();
  if (dur == 0)
    dur = 1;

  int progress = (int32_t)(pos * 100 / dur);
  if (force || progress != s_last_progress) {
    lv_bar_set_value(s_progress_bar, progress, LV_ANIM_OFF);
    s_last_progress = progress;
  }

  uint8_t vol         = audio_get_volume();
  bool    vol_changed = force || vol != s_last_vol;
  if (vol_changed) {
    lv_bar_set_value(s_vol_bar, vol, LV_ANIM_OFF);
    s_last_vol = vol;
  }
  if (vol_changed && s_vol_pct_label && lv_obj_is_valid(s_vol_pct_label))
    lv_label_set_text_fmt(s_vol_pct_label, "%u%%", vol);

  uint32_t pos_sec = pos / 1000;
  uint32_t dur_sec = dur / 1000;
  if (force || pos_sec != s_last_pos_sec || dur_sec != s_last_dur_sec) {
    lv_label_set_text_fmt(s_time_label, "%lu:%02lu / %lu:%02lu",
                          (unsigned long)(pos_sec / 60),
                          (unsigned long)(pos_sec % 60),
                          (unsigned long)(dur_sec / 60),
                          (unsigned long)(dur_sec % 60));
    s_last_pos_sec = pos_sec;
    s_last_dur_sec = dur_sec;
  }

  if (s_track_label && lv_obj_is_valid(s_track_label) && s_playlist_count > 0)
    if (force || s_last_track_index != s_current_index ||
        s_last_track_count != s_playlist_count) {
      lv_label_set_text_fmt(s_track_label, "%d / %d", s_current_index + 1,
                            s_playlist_count);
      s_last_track_index = s_current_index;
      s_last_track_count = s_playlist_count;
    }

  bool paused  = audio_is_paused();
  bool playing = audio_is_playing();
  if (force || paused != s_last_paused || playing != s_last_playing) {
    if (paused)
      lv_label_set_text(s_status_label, LV_SYMBOL_PAUSE " Pause");
    else if (playing)
      lv_label_set_text(s_status_label, LV_SYMBOL_PLAY " Playing");
    else {
      lv_label_set_text(s_status_label, LV_SYMBOL_STOP " Stop");
      PREVIEW_AUDIO_LOGW("update_ui: trans to STOP! prev_paused=%d prev_playing=%d",
                         s_last_paused, s_last_playing);
    }
    s_last_paused  = paused;
    s_last_playing = playing;
  }

  if (s_tech_label && lv_obj_is_valid(s_tech_label)) {
    audio_track_info_t ti;
    if (audio_get_track_info(&ti)) {
      bool changed = force || ti.type != s_last_track_type ||
                     ti.sample_rate_hz != s_last_sample_rate_hz ||
                     ti.channels != s_last_channels ||
                     ti.bits_per_sample != s_last_bits_per_sample ||
                     ti.bitrate_kbps != s_last_bitrate_kbps ||
                     ti.is_float != s_last_is_float ||
                     ti.mp3_vbr != s_last_mp3_vbr;
      if (changed) {
        uint32_t sr10  = ti.sample_rate_hz / 100;
        uint32_t sr_i  = sr10 / 10;
        uint32_t sr_f  = sr10 % 10;
        if (ti.type == AUDIO_TRACK_TYPE_MP3) {
          if (ti.bitrate_kbps > 0 && ti.mp3_vbr)
            lv_label_set_text_fmt(s_tech_label, "MP3 %lu.%lukHz %ukbps VBR %uch",
                                  (unsigned long)sr_i, (unsigned long)sr_f,
                                  (unsigned)ti.bitrate_kbps,
                                  (unsigned)ti.channels);
          else if (ti.bitrate_kbps > 0)
            lv_label_set_text_fmt(s_tech_label, "MP3 %lu.%lukHz %ukbps %uch",
                                  (unsigned long)sr_i, (unsigned long)sr_f,
                                  (unsigned)ti.bitrate_kbps,
                                  (unsigned)ti.channels);
          else
            lv_label_set_text_fmt(s_tech_label, "MP3 %lu.%lukHz %uch",
                                  (unsigned long)sr_i, (unsigned long)sr_f,
                                  (unsigned)ti.channels);
        } else if (ti.type == AUDIO_TRACK_TYPE_WAV) {
          if (ti.is_float && ti.bits_per_sample > 0)
            lv_label_set_text_fmt(s_tech_label, "WAV %lu.%lukHz F%u %uch",
                                  (unsigned long)sr_i, (unsigned long)sr_f,
                                  (unsigned)ti.bits_per_sample,
                                  (unsigned)ti.channels);
          else if (ti.bits_per_sample > 0)
            lv_label_set_text_fmt(s_tech_label, "WAV %lu.%lukHz %ub %uch",
                                  (unsigned long)sr_i, (unsigned long)sr_f,
                                  (unsigned)ti.bits_per_sample,
                                  (unsigned)ti.channels);
          else
            lv_label_set_text_fmt(s_tech_label, "WAV %lu.%lukHz %uch",
                                  (unsigned long)sr_i, (unsigned long)sr_f,
                                  (unsigned)ti.channels);
        } else {
          lv_label_set_text(s_tech_label, "");
        }

        s_last_track_type      = ti.type;
        s_last_sample_rate_hz  = ti.sample_rate_hz;
        s_last_channels        = ti.channels;
        s_last_bits_per_sample = ti.bits_per_sample;
        s_last_bitrate_kbps    = ti.bitrate_kbps;
        s_last_is_float        = ti.is_float;
        s_last_mp3_vbr         = ti.mp3_vbr;
      }
    }
  }
}

static void preview_audio_apply_volume_delta(int delta) {
  if (delta == 0)
    return;
  int v = (int)audio_get_volume() + delta;
  if (v < 0)
    v = 0;
  if (v > 100)
    v = 100;
  audio_set_volume((uint8_t)v);
  ui_settings_sync_volume((uint8_t)v);
  preview_audio_update_ui(true);
}

static void preview_audio_apply_seek_delta(int delta_seconds) {
  if (delta_seconds == 0)
    return;

  uint32_t pos_ms = audio_get_position_ms();
  uint32_t dur_ms = audio_get_duration_ms();
  int clamped_delta = delta_seconds;

  if (delta_seconds > 0 && dur_ms > 0) {
    if (pos_ms >= dur_ms)
      return;
    uint32_t remaining_ms = dur_ms - pos_ms;
    int max_forward_sec = (int)((remaining_ms - 1) / 1000);
    if (max_forward_sec <= 0)
      return;
    if (clamped_delta > max_forward_sec)
      clamped_delta = max_forward_sec;
  } else if (delta_seconds < 0) {
    int max_backward_sec = (int)(pos_ms / 1000);
    if (max_backward_sec <= 0)
      return;
    if (-clamped_delta > max_backward_sec)
      clamped_delta = -max_backward_sec;
  }

  if (clamped_delta == 0)
    return;

  audio_seek_seconds(clamped_delta);
  preview_audio_update_ui(true);
}

static void preview_audio_reset_adjust_repeats(void) {
  input_repeat_reset(&s_volume_repeat);
  input_repeat_reset(&s_seek_repeat);
}

static void preview_audio_volume_hold_tick(const input_gamepad_state *gp) {
  bool up   = gp->values[GAMEPAD_INPUT_UP] == 1;
  bool down = gp->values[GAMEPAD_INPUT_DOWN] == 1;

  int held_count = (up ? 1 : 0) + (down ? 1 : 0);
  if (held_count != 1) {
    input_repeat_reset(&s_volume_repeat);
    return;
  }

  uint32_t now = lv_tick_get();
  uint32_t dir = up ? GAMEPAD_INPUT_UP : GAMEPAD_INPUT_DOWN;
  uint16_t repeat_count = 0;
  if (!input_repeat_tick(&s_volume_repeat, true, dir, now,
                         &s_audio_adjust_repeat, &repeat_count))
    return;
  int scale = input_repeat_scale_for_count(&s_audio_adjust_repeat, repeat_count);
  preview_audio_apply_volume_delta((up ? 1 : -1) * scale);
}

static void preview_audio_seek_hold_tick(const input_gamepad_state *gp) {
  bool left = gp->values[GAMEPAD_INPUT_LEFT] == 1;
  bool right = gp->values[GAMEPAD_INPUT_RIGHT] == 1;

  int held_count = (left ? 1 : 0) + (right ? 1 : 0);
  if (held_count != 1) {
    input_repeat_reset(&s_seek_repeat);
    return;
  }

  uint32_t now = lv_tick_get();
  uint32_t dir = right ? GAMEPAD_INPUT_RIGHT : GAMEPAD_INPUT_LEFT;
  uint16_t repeat_count = 0;
  if (!input_repeat_tick(&s_seek_repeat, true, dir, now,
                         &s_audio_adjust_repeat, &repeat_count))
    return;

  int scale = input_repeat_scale_for_count(&s_audio_adjust_repeat, repeat_count);
  int delta = AUDIO_SEEK_STEP_SECONDS * scale;
  preview_audio_apply_seek_delta((right ? 1 : -1) * delta);
}

/* ------------------------------------------------------------------ playback */

static void preview_audio_start_track(const char *path) {
  bool was_playing = audio_is_playing();
  PREVIEW_AUDIO_LOGI("start_track ENTER: path=%s was_playing=%d",
                     path, was_playing);
  strlcpy(s_current_path, path, sizeof(s_current_path));
  s_track_confirmed_playing = false;
  preview_audio_ensure_session_timer();
  preview_audio_persist_state(true);
  audio_play_file_async(path);
  bool now_playing = audio_is_playing();

  const char *bn = fm_base_name(path);
  ui_chrome_set_title(&s_chrome, bn);

  mp3_tags_t tags;
  bool has_tags = mp3_read_tags(path, &tags);
  if (s_tag_title_label && lv_obj_is_valid(s_tag_title_label))
    lv_label_set_text(s_tag_title_label,
                      has_tags && tags.title[0] ? tags.title : bn);
  if (s_tag_artist_label && lv_obj_is_valid(s_tag_artist_label)) {
    if (has_tags && (tags.artist[0] || tags.album[0])) {
      if (tags.artist[0] && tags.album[0])
        lv_label_set_text_fmt(s_tag_artist_label, "%s - %s",
                              tags.artist, tags.album);
      else if (tags.artist[0])
        lv_label_set_text(s_tag_artist_label, tags.artist);
      else
        lv_label_set_text(s_tag_artist_label, tags.album);
    } else {
      lv_label_set_text(s_tag_artist_label, "");
    }
  }

  PREVIEW_AUDIO_LOGI("start_track EXIT: now_playing=%d", now_playing);
  preview_audio_update_ui(true);
}

static bool preview_audio_build_path(char *out, size_t out_sz, const char *name) {
  if (!out || out_sz == 0 || !name || name[0] == '\0' || s_playlist_cwd[0] == '\0')
    return false;
  strlcpy(out, s_playlist_cwd, out_sz);
  if (strlcat(out, "/", out_sz) >= out_sz)
    return false;
  if (strlcat(out, name, out_sz) >= out_sz)
    return false;
  return true;
}

static void preview_audio_play_index(int idx) {
  if (s_playlist_count == 0)
    return;
  while (idx < 0)
    idx += s_playlist_count;
  idx %= s_playlist_count;
  s_current_index = idx;

  char full[AUDIO_PATH_MAX];
  if (!preview_audio_build_path(full, sizeof(full), s_playlist[s_current_index]))
    return;

  PREVIEW_AUDIO_LOGI("play_index idx=%d playlist=%s count=%d full=%s",
                     idx, s_playlist[idx], s_playlist_count, full);
  preview_audio_start_track(full);
}

static bool preview_audio_prepare_session(const char *path, char *shared_names,
                                          int shared_count, int shared_index,
                                          int shared_name_stride,
                                          const char *cwd_override) {
  if (!path || path[0] == '\0')
    return false;

  s_playlist = NULL;
  s_playlist_count = 0;
  s_current_index = 0;
  s_shuffle_pos = 0;
  s_playlist_cwd[0] = '\0';
  s_playlist_from_shared = false;
  s_close_to_background = false;
  strlcpy(s_current_path, path, sizeof(s_current_path));

  preview_open_args_t args = {
      .cwd = cwd_override,
      .shared_names = shared_names,
      .shared_count = shared_count,
      .shared_index = shared_index,
      .shared_name_stride = shared_name_stride,
  };

  if (!preview_audio_build_playlist(&args, path)) {
    return false;
  }
  s_session_active = true;
  s_track_confirmed_playing = false;
  ui_chrome_set_music_active(true);
  return true;
}

static bool preview_audio_build_foreground(lv_obj_t *screen) {
  if (!screen)
    return false;

  ui_chrome_detach(&s_chrome);
  lv_obj_clean(screen);
  ui_theme_apply_screen(screen);

  s_chrome = ui_chrome_create(screen, "Music Player");

  lv_obj_t *card = lv_obj_create(screen);
  lv_obj_remove_style_all(card);
  ui_theme_style_panel(card);
  lv_obj_set_size(card, 308, 140);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top() + 2);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(card, 4, 0);
  lv_obj_set_style_pad_row(card, 0, 0);

  s_tag_title_label = lv_label_create(card);
  lv_obj_set_width(s_tag_title_label, 288);
  lv_label_set_long_mode(s_tag_title_label, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(s_tag_title_label, fm_base_name(s_current_path));
  ui_theme_style_label_primary(s_tag_title_label);

  s_tag_artist_label = lv_label_create(card);
  lv_obj_set_width(s_tag_artist_label, 288);
  lv_label_set_long_mode(s_tag_artist_label,  LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(s_tag_artist_label, "");
  ui_theme_style_label_secondary(s_tag_artist_label);

  s_tech_label = lv_label_create(card);
  lv_obj_set_width(s_tech_label, 288);
  lv_label_set_long_mode(s_tech_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_label_set_text(s_tech_label, "");
  ui_theme_style_label_secondary(s_tech_label);

  lv_obj_t *status_row = lv_obj_create(card);
  lv_obj_remove_style_all(status_row);
  lv_obj_set_width(status_row, 288);
  lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(status_row, 4, 0);

  s_track_label = lv_label_create(status_row);
  lv_label_set_text(s_track_label, "1 / 1");
  ui_theme_style_label_secondary(s_track_label);

  s_status_label = lv_label_create(status_row);
  lv_label_set_text(s_status_label, LV_SYMBOL_PLAY " Playing");
  ui_theme_style_label_primary(s_status_label);

  s_mode_label = lv_label_create(status_row);
  lv_label_set_text(s_mode_label, play_mode_text(s_play_mode));
  ui_theme_style_label_secondary(s_mode_label);

  s_eq_label = lv_label_create(status_row);
  lv_label_set_text(s_eq_label, eq_preset_name(eq_get_preset()));
  ui_theme_style_label_accent(s_eq_label);

  s_time_label = lv_label_create(screen);
  lv_label_set_text(s_time_label, "0:00 / 0:00");
  ui_theme_style_label_secondary(s_time_label);
  lv_obj_align(s_time_label, LV_ALIGN_BOTTOM_LEFT, 8, -50);

  s_progress_bar = lv_bar_create(screen);
  lv_obj_set_size(s_progress_bar, 308, 10);
  lv_bar_set_range(s_progress_bar, 0, 100);
  ui_theme_style_bar(s_progress_bar);
  lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -34);

  lv_obj_t *vol_row = lv_obj_create(screen);
  lv_obj_remove_style_all(vol_row);
  lv_obj_set_size(vol_row, 308, 22);
  lv_obj_align(vol_row, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_flex_flow(vol_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(vol_row, 6, 0);

  lv_obj_t *vol_lbl = lv_label_create(vol_row);
  lv_label_set_text(vol_lbl, LV_SYMBOL_VOLUME_MAX);
  ui_theme_style_label_accent(vol_lbl);

  s_vol_bar = lv_bar_create(vol_row);
  lv_obj_set_size(s_vol_bar, 210, 10);
  lv_bar_set_range(s_vol_bar, 0, 100);
  ui_theme_style_bar(s_vol_bar);
  lv_obj_set_flex_grow(s_vol_bar, 1);

  s_vol_pct_label = lv_label_create(vol_row);
  lv_label_set_text(s_vol_pct_label, "50%");
  ui_theme_style_label_secondary(s_vol_pct_label);
  lv_obj_set_width(s_vol_pct_label, 42);

  s_active = true;
  s_last_ui_ms = 0;
  s_last_vol = 0xFF;
  s_last_progress = -1;
  s_last_pos_sec = UINT32_MAX;
  s_last_dur_sec = UINT32_MAX;
  s_last_paused = false;
  s_last_playing = false;
  s_last_track_index = -1;
  s_last_track_count = -1;
  s_last_track_type = AUDIO_TRACK_TYPE_NONE;
  s_last_sample_rate_hz = UINT32_MAX;
  s_last_channels = UINT16_MAX;
  s_last_bits_per_sample = UINT16_MAX;
  s_last_bitrate_kbps = UINT16_MAX;
  s_last_is_float = false;
  s_last_mp3_vbr = false;
  preview_audio_reset_adjust_repeats();
  preview_audio_update_ui(true);
  return true;
}

/* ------------------------------------------------------------------ open */

static bool preview_audio_open(const char *path, preview_open_args_t *args) {
  if (!path || !args || !args->screen)
    return false;

  if (s_session_active)
    preview_audio_stop_session_internal();

  if (!preview_audio_prepare_session(path, (char *)args->shared_names,
                                     args->shared_count, args->shared_index,
                                     args->shared_name_stride, args->cwd))
    return false;
  args->shared_names = NULL;
  if (!preview_audio_build_foreground(args->screen)) {
    preview_audio_stop_session_internal();
    return false;
  }
  preview_audio_start_track(path);

  return true;
}

/* ------------------------------------------------------------------ close */

static void preview_audio_close_foreground(void) {
  ui_chrome_detach(&s_chrome);
  preview_audio_reset_adjust_repeats();
  s_active = false;
  s_tag_title_label = NULL;
  s_tag_artist_label = NULL;
  s_tech_label = NULL;
  s_track_label = NULL;
  s_time_label = NULL;
  s_status_label = NULL;
  s_progress_bar = NULL;
  s_vol_bar = NULL;
  s_vol_pct_label = NULL;
  s_mode_label = NULL;
  s_eq_label = NULL;
}

static void preview_audio_reset_session_state(void) {
  preview_audio_free_playlist();
  preview_audio_close_foreground();
  preview_audio_drop_session_timer();
  s_playlist_count = 0;
  s_current_index = 0;
  s_shuffle_pos = 0;
  s_current_path[0] = '\0';
  s_playlist_cwd[0] = '\0';
  s_playlist_from_shared = false;
  s_session_active = false;
  s_close_to_background = false;
  s_track_confirmed_playing = false;
  ui_chrome_set_music_active(false);
}

static void preview_audio_stop_session_internal(void) {
  audio_stop_playback();
  uint32_t start = platform_millis();
  while (audio_is_playing() && (platform_millis() - start) < 500)
    preview_audio_wait_ms(10);
  preview_audio_persist_state(false);
  preview_audio_reset_session_state();
}

static void preview_audio_close(void) {
  if (s_close_to_background) {
    preview_audio_close_foreground();
    s_close_to_background = false;
    return;
  }
  preview_audio_stop_session_internal();
}

/* ------------------------------------------------------------------ key handler */

static bool preview_audio_on_key(const input_gamepad_state *gp,
                                 const bool edge[]) {
  if (!s_active)
    return false;

  if ((edge[GAMEPAD_INPUT_A] && gp->values[GAMEPAD_INPUT_B] == 1) ||
      (edge[GAMEPAD_INPUT_B] && gp->values[GAMEPAD_INPUT_A] == 1)) {
    s_close_to_background = false;
    preview_audio_sync_file_manager_cwd();
    fm_handle_back();
    return true;
  }
  if (edge[GAMEPAD_INPUT_B]) {
    s_close_to_background = true;
    preview_audio_sync_file_manager_cwd();
    fm_handle_back();
    return true;
  }

  if (edge[GAMEPAD_INPUT_UP]) {
    preview_audio_apply_volume_delta(1);
    input_repeat_arm(&s_volume_repeat, GAMEPAD_INPUT_UP, lv_tick_get(),
                     &s_audio_adjust_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_DOWN]) {
    preview_audio_apply_volume_delta(-1);
    input_repeat_arm(&s_volume_repeat, GAMEPAD_INPUT_DOWN, lv_tick_get(),
                     &s_audio_adjust_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_LEFT]) {
    preview_audio_apply_seek_delta(-AUDIO_SEEK_STEP_SECONDS);
    input_repeat_arm(&s_seek_repeat, GAMEPAD_INPUT_LEFT, lv_tick_get(),
                     &s_audio_adjust_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_RIGHT]) {
    preview_audio_apply_seek_delta(AUDIO_SEEK_STEP_SECONDS);
    input_repeat_arm(&s_seek_repeat, GAMEPAD_INPUT_RIGHT, lv_tick_get(),
                     &s_audio_adjust_repeat);
    return true;
  }
  if (edge[GAMEPAD_INPUT_L]) {
    if (s_play_mode == PLAY_MODE_SHUFFLE)
      preview_audio_play_index(preview_audio_shuffle_step(-1, false));
    else
      preview_audio_play_index(s_current_index - 1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_R]) {
    if (s_play_mode == PLAY_MODE_SHUFFLE)
      preview_audio_play_index(preview_audio_shuffle_step(1, false));
    else
      preview_audio_play_index(s_current_index + 1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_START]) {
    eq_next_preset();
    hal_settings_save(SettingEqPreset, (int32_t)eq_get_preset());
    if (s_eq_label && lv_obj_is_valid(s_eq_label))
      lv_label_set_text(s_eq_label, eq_preset_name(eq_get_preset()));
    return true;
  }
  if (edge[GAMEPAD_INPUT_A]) {
    bool p = audio_is_paused(), pl = audio_is_playing();
    PREVIEW_AUDIO_LOGI("key: A toggle pause p=%d playing=%d", p, pl);
    if (!pl && !p)
      preview_audio_play_index(s_current_index);
    else
      audio_toggle_pause();
    preview_audio_update_ui(true);
    return true;
  }
  if (edge[GAMEPAD_INPUT_SELECT]) {
    s_play_mode = (play_mode_t)((s_play_mode + 1) % PLAY_MODE_COUNT);
    if (s_play_mode == PLAY_MODE_SHUFFLE)
      preview_audio_shuffle_reset(s_current_index);
    update_mode_label();
    return true;
  }
  if (edge[GAMEPAD_INPUT_MENU]) {
    s_close_to_background = true;
    preview_audio_sync_file_manager_cwd();
    preview_close();
    ui_home_create();
    return true;
  }

  preview_audio_volume_hold_tick(gp);
  preview_audio_seek_hold_tick(gp);
  (void)gp;
  return false;
}

/* ------------------------------------------------------------------ timer */

static void preview_audio_session_tick(bool force_ui) {
  if (!s_session_active)
    return;

  bool playing = audio_is_playing();
  bool paused  = audio_is_paused();

  if (!s_track_confirmed_playing && playing)
    s_track_confirmed_playing = true;

  if (s_track_confirmed_playing && !playing && !paused) {
    PREVIEW_AUDIO_LOGI("timer: track-end detected, mode=%d idx=%d cnt=%d",
                       s_play_mode, s_current_index, s_playlist_count);
    s_track_confirmed_playing = false;
    s_last_playing = false; /* prevent stale UI state triggering on next tick */
    switch (s_play_mode) {
      case PLAY_MODE_LIST_LOOP:
        preview_audio_play_index((s_current_index + 1) % s_playlist_count);
        return;
      case PLAY_MODE_SHUFFLE:
        preview_audio_play_index(preview_audio_shuffle_step(1, true));
        return;
      case PLAY_MODE_SINGLE_LOOP:
        preview_audio_play_index(s_current_index);
        return;
      case PLAY_MODE_LIST_PLAY:
        if (s_current_index < s_playlist_count - 1) {
          preview_audio_play_index(s_current_index + 1);
          return;
        }
        /* Last track finished — fall through to update UI (shows Stop). */
        break;
      default:
        break;
    }
  }

  if (s_active)
    preview_audio_update_ui(force_ui);
}

static void preview_audio_session_timer_cb(lv_timer_t *timer) {
  (void)timer;
  preview_audio_session_tick(false);
}

static void preview_audio_on_timer(void) {
  preview_audio_session_tick(false);
}

static const char *preview_audio_current_path(void) {
  return s_session_active ? s_current_path : NULL;
}

bool preview_audio_session_is_active(void) { return s_session_active; }

bool preview_audio_session_is_playback_live(void) {
  return s_session_active && !s_active && (audio_is_playing() || audio_is_paused());
}

const char *preview_audio_session_current_path(void) {
  return s_session_active ? s_current_path : NULL;
}

bool preview_audio_session_switch_track(int delta) {
  if (!preview_audio_session_is_playback_live() || delta == 0)
    return false;
  if (s_play_mode == PLAY_MODE_SHUFFLE)
    preview_audio_play_index(preview_audio_shuffle_step((delta > 0) ? 1 : -1, false));
  else
    preview_audio_play_index(s_current_index + ((delta > 0) ? 1 : -1));
  return true;
}

void preview_audio_restore_persisted_session(void) {
  int32_t armed = 0;
  if (s_session_active || hal_settings_load(SettingMusicSessionArmed, &armed) != 0 ||
      armed == 0)
    return;

  char *saved_path = hal_settings_load_str(SettingMusicSessionPath);
  if (!saved_path || saved_path[0] == '\0') {
    free(saved_path);
    return;
  }
  if (!preview_audio_path_exists(saved_path)) {
    free(saved_path);
    return;
  }

  if (!preview_audio_prepare_session(saved_path, NULL, 0, 0, FM_NAME_LEN, NULL))
    preview_audio_reset_session_state();
  free(saved_path);
}

bool preview_audio_restore_foreground(lv_obj_t *screen, lv_group_t *input_group) {
  (void)input_group;
  if (!s_session_active || s_active)
    return false;
  if (!preview_audio_build_foreground(screen))
    return false;
  preview_set_active(&preview_audio_app);
  preview_audio_session_tick(true);
  return true;
}

void preview_audio_stop_session(void) {
  if (s_active) {
    s_close_to_background = false;
    preview_close();
    return;
  }
  preview_audio_stop_session_internal();
}

/* ------------------------------------------------------------------ export */

const preview_app_t preview_audio_app = {
    .id       = "audio",
    .can_open = preview_audio_can_open,
    .open     = preview_audio_open,
    .close    = preview_audio_close,
    .on_key   = preview_audio_on_key,
    .on_timer = preview_audio_on_timer,
    .current_path = preview_audio_current_path,
};
