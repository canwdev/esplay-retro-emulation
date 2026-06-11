#include "preview_audio.h"

#ifdef TARGET_SIM

#include "file_manager.h"
#include "platform_mem.h"
#include "ui_backlight.h"
#include "ui_chrome.h"
#include "ui_theme.h"
#include "sim_compat.h"

#include "lvgl.h"

static const char *TAG = "preview_audio";
static ui_chrome_t s_chrome;
static lv_obj_t *s_body_label;
static char s_current_path[FM_PATH_MAX];
static char *s_shared_names;
static int s_shared_count;
static int s_shared_index;
static int s_shared_name_stride;
static char s_shared_cwd[FM_PATH_MAX];
static bool s_opened;

static const char *preview_audio_current_path(void) {
  return s_opened ? s_current_path : NULL;
}

static const char *preview_audio_shared_name_at(int index) {
  if (!s_shared_names || s_shared_count <= 0 || s_shared_name_stride <= 0)
    return NULL;
  if (index < 0 || index >= s_shared_count)
    return NULL;
  return s_shared_names + (size_t)index * (size_t)s_shared_name_stride;
}

static void preview_audio_release_shared_list(void) {
  platform_free(s_shared_names);
  s_shared_names = NULL;
  s_shared_count = 0;
  s_shared_index = -1;
  s_shared_name_stride = 0;
  s_shared_cwd[0] = '\0';
}

static void preview_audio_set_path(const char *path) {
  strlcpy(s_current_path, path ? path : "", sizeof(s_current_path));
  if (s_chrome.title_label)
    lv_label_set_text(s_chrome.title_label, fm_base_name(s_current_path));
}

static void preview_audio_switch_relative(int delta) {
  char full[FM_PATH_MAX];
  const char *name;
  int next;

  if (s_shared_count <= 1)
    return;
  next = s_shared_index + delta;
  if (next < 0)
    next = s_shared_count - 1;
  else if (next >= s_shared_count)
    next = 0;
  name = preview_audio_shared_name_at(next);
  if (!name || !name[0])
    return;
  if (snprintf(full, sizeof(full), "%s/%s", s_shared_cwd, name) < 0 ||
      strlen(full) >= sizeof(full))
    return;
  s_shared_index = next;
  preview_audio_set_path(full);
}

static bool preview_audio_can_open(const char *path) {
  return fm_is_playable_audio_filename(fm_base_name(path));
}

static bool preview_audio_open(const char *path, preview_open_args_t *args) {
  memset(s_current_path, 0, sizeof(s_current_path));
  preview_audio_release_shared_list();
  if (args->cwd)
    strlcpy(s_shared_cwd, args->cwd, sizeof(s_shared_cwd));
  s_shared_count = args->shared_count;
  s_shared_index = args->shared_index;
  s_shared_name_stride = args->shared_name_stride;
  if (args->shared_names && args->shared_count > 0 && args->shared_name_stride > 0) {
    s_shared_names = (char *)args->shared_names;
    args->shared_names = NULL;
  }

  lv_obj_clean(args->screen);
  ui_theme_apply_screen(args->screen);
  ui_chrome_detach(&s_chrome);
  s_chrome = ui_chrome_create(args->screen, fm_base_name(path));

  s_body_label = lv_label_create(args->screen);
  ui_theme_style_label_primary(s_body_label);
  lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(s_body_label, 300);
  lv_label_set_text(s_body_label,
                    "Audio preview is not supported in the simulator.\n"
                    "Use real hardware for playback validation.");
  lv_obj_align(s_body_label, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top() + 10);

  preview_audio_set_path(path);
  (void)TAG;
  s_opened = true;
  return true;
}

static void preview_audio_close(void) {
  preview_audio_release_shared_list();
  ui_chrome_detach(&s_chrome);
  s_body_label = NULL;
  s_current_path[0] = '\0';
  s_opened = false;
}

static bool preview_audio_on_key(const input_gamepad_state *gp,
                                const bool edge[]) {
  (void)gp;
  if (!s_opened)
    return false;
  if (edge[GAMEPAD_INPUT_MENU]) {
    ui_backlight_toggle();
    return true;
  }
  if (edge[GAMEPAD_INPUT_L]) {
    preview_audio_switch_relative(-1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_R]) {
    preview_audio_switch_relative(1);
    return true;
  }
  return false;
}

static void preview_audio_on_timer(void) {
}

const preview_app_t preview_audio_app = {
    .id       = "audio",
    .can_open = preview_audio_can_open,
    .open     = preview_audio_open,
    .close    = preview_audio_close,
    .on_key   = preview_audio_on_key,
    .on_timer = preview_audio_on_timer,
    .current_path = preview_audio_current_path,
};

#else

#include "audio.h"
#include "file_manager.h"
#include "lcd.h"
#include "settings.h"
#include "ui_settings.h"
#include "ui_chrome.h"
#include "ui_theme.h"
#include "platform_mem.h"
#include "esp_log.h"
#include "esp_random.h"
#include "ui_backlight.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define AUDIO_PLAYLIST_MAX 512
#define AUDIO_PATH_MAX     256
#define AUDIO_VOL_HOLD_MS_INITIAL 400
#define AUDIO_VOL_HOLD_MS_REPEAT  80

typedef enum {
  PLAY_MODE_LIST_LOOP = 0, /* wrap around at end of list  (default) */
  PLAY_MODE_SHUFFLE,       /* Fisher-Yates shuffled cycle, no repeat per round */
  PLAY_MODE_SINGLE_LOOP,   /* replay the same track forever          */
  PLAY_MODE_LIST_PLAY,     /* play to last track then stop           */
  PLAY_MODE_COUNT,
} play_mode_t;

static char (*s_playlist)[FM_NAME_LEN] = NULL;
static int  s_playlist_count;
static int  s_current_index;
static int  s_shuffle_order[AUDIO_PLAYLIST_MAX];
static int  s_shuffle_pos;
static char s_current_path[AUDIO_PATH_MAX];
static char s_playlist_cwd[AUDIO_PATH_MAX];
static bool s_playlist_from_shared;
static bool s_active;

static ui_chrome_t s_chrome;
static lv_obj_t *s_filename_label;
static lv_obj_t *s_tech_label;
static lv_obj_t *s_track_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_vol_bar;
static lv_obj_t *s_vol_pct_label;
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
static bool s_vol_hold_armed;
static int8_t s_vol_hold_dir;
static uint32_t s_vol_hold_next_ms;
static const char *preview_audio_current_path(void);

/* ------------------------------------------------------------------ helpers */

static const char *play_mode_text(play_mode_t m) {
  switch (m) {
    case PLAY_MODE_LIST_LOOP:   return LV_SYMBOL_LOOP " All";
    case PLAY_MODE_SHUFFLE:     return LV_SYMBOL_SHUFFLE " Rand";
    case PLAY_MODE_SINGLE_LOOP: return LV_SYMBOL_LOOP " x1";
    case PLAY_MODE_LIST_PLAY:   return LV_SYMBOL_NEXT " Seq";
    default: return "";
  }
}

static void update_mode_label(void) {
  if (!s_mode_label || !lv_obj_is_valid(s_mode_label))
    return;
  lv_label_set_text(s_mode_label, play_mode_text(s_play_mode));
}

/* ------------------------------------------------------------------ can_open */

static bool preview_audio_can_open(const char *path) {
  return fm_is_playable_audio_filename(fm_base_name(path));
}

/* ------------------------------------------------------------------ playlist */

static int preview_playlist_compare(const void *a, const void *b) {
  return strcasecmp((const char *)a, (const char *)b);
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
    uint32_t r = esp_random();
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

static void preview_audio_build_playlist(preview_open_args_t *args,
                                         const char *current) {
  s_playlist_count = 0;
  s_current_index  = 0;
  s_shuffle_pos    = 0;

  if (args->cwd)
    strlcpy(s_playlist_cwd, args->cwd, sizeof(s_playlist_cwd));

  if (args->shared_names && args->shared_count > 0 && args->shared_name_stride == FM_NAME_LEN) {
    s_playlist = (char (*)[FM_NAME_LEN])args->shared_names;
    args->shared_names = NULL;
    s_playlist_from_shared = true;
    s_playlist_count = args->shared_count;
    s_current_index = args->shared_index >= 0 ? args->shared_index : 0;
    if (s_current_index >= s_playlist_count)
      s_current_index = 0;
    preview_audio_shuffle_reset(s_current_index);
    return;
  }

  DIR *dir = opendir(s_playlist_cwd);
  if (!dir)
    return;

  struct dirent *de;
  while ((de = readdir(dir)) != NULL && s_playlist_count < AUDIO_PLAYLIST_MAX) {
    if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN)
      continue;
    if (!fm_is_playable_audio_filename(de->d_name))
      continue;
    strlcpy(s_playlist[s_playlist_count], de->d_name, FM_NAME_LEN);
    s_playlist_count++;
  }
  closedir(dir);

  if (s_playlist_count > 1)
    qsort(s_playlist, s_playlist_count, FM_NAME_LEN, preview_playlist_compare);

  const char *cur = fm_base_name(current);
  for (int i = 0; i < s_playlist_count; i++) {
    if (strcasecmp(s_playlist[i], cur) == 0) {
      s_current_index = i;
      break;
    }
  }

  preview_audio_shuffle_reset(s_current_index);
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
      ESP_LOGW("preview_audio", "update_ui: trans to STOP! prev_paused=%d prev_playing=%d",
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

static void preview_audio_vol_hold_reset(void) {
  s_vol_hold_armed = false;
  s_vol_hold_dir   = 0;
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

static void preview_audio_vol_hold_tick(const input_gamepad_state *gp) {
  bool up   = gp->values[GAMEPAD_INPUT_UP] == 1;
  bool down = gp->values[GAMEPAD_INPUT_DOWN] == 1;

  if ((up && down) || (!up && !down)) {
    preview_audio_vol_hold_reset();
    return;
  }

  int8_t dir = up ? 1 : -1;
  uint32_t now = lv_tick_get();

  if (!s_vol_hold_armed || s_vol_hold_dir != dir) {
    s_vol_hold_armed   = true;
    s_vol_hold_dir     = dir;
    s_vol_hold_next_ms = now + AUDIO_VOL_HOLD_MS_INITIAL;
    return;
  }

  if ((int32_t)(now - s_vol_hold_next_ms) < 0)
    return;

  preview_audio_apply_volume_delta(dir);
  s_vol_hold_next_ms = now + AUDIO_VOL_HOLD_MS_REPEAT;
}

static void preview_audio_arm_volume_hold(int8_t dir) {
  s_vol_hold_armed   = true;
  s_vol_hold_dir     = dir;
  s_vol_hold_next_ms = lv_tick_get() + AUDIO_VOL_HOLD_MS_INITIAL;
}

/* ------------------------------------------------------------------ playback */

static void preview_audio_start_track(const char *path) {
  bool was_playing = audio_is_playing();
  ESP_LOGI("preview_audio", "start_track ENTER: path=%s was_playing=%d",
           path, was_playing);
  strlcpy(s_current_path, path, sizeof(s_current_path));
  s_track_confirmed_playing = false;
  audio_play_file_async(path);
  bool now_playing = audio_is_playing();
  if (s_filename_label && lv_obj_is_valid(s_filename_label))
    lv_label_set_text(s_filename_label, fm_base_name(path));
  ESP_LOGI("preview_audio", "start_track EXIT: now_playing=%d", now_playing);
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

  ESP_LOGI("preview_audio", "play_index idx=%d playlist=%s count=%d full=%s",
           idx, s_playlist[idx], s_playlist_count, full);
  preview_audio_start_track(full);
}

/* ------------------------------------------------------------------ open */

static bool preview_audio_open(const char *path, preview_open_args_t *args) {
  s_playlist = NULL;
  s_playlist_count = 0;
  s_current_index = 0;
  s_shuffle_pos = 0;
  s_playlist_cwd[0] = '\0';
  s_playlist_from_shared = false;

  if (!args->shared_names) {
    s_playlist = malloc(AUDIO_PLAYLIST_MAX * FM_NAME_LEN);
    if (!s_playlist) {
      ESP_LOGE("preview_audio", "Failed to allocate memory for s_playlist");
      return false;
    }
  }

  preview_audio_build_playlist(args, path);

  ui_chrome_detach(&s_chrome);
  lv_obj_clean(args->screen);
  ui_theme_apply_screen(args->screen);

  s_chrome = ui_chrome_create(args->screen, "Music Player");

  /* ---- info card ---- */
  lv_obj_t *card = lv_obj_create(args->screen);
  lv_obj_remove_style_all(card);
  ui_theme_style_panel(card);
  lv_obj_set_size(card, 308, 140);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top() + 2);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(card, 4, 0);
  lv_obj_set_style_pad_row(card, 0, 0);

  s_filename_label = lv_label_create(card);
  lv_obj_set_width(s_filename_label, 288);
  lv_obj_set_height(s_filename_label, 36);
  lv_label_set_long_mode(s_filename_label, LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_text(s_filename_label, fm_base_name(path));
  ui_theme_style_label_primary(s_filename_label);

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

  /* ---- time / progress / volume ---- */
  s_time_label = lv_label_create(args->screen);
  lv_label_set_text(s_time_label, "0:00 / 0:00");
  ui_theme_style_label_secondary(s_time_label);
  lv_obj_align(s_time_label, LV_ALIGN_BOTTOM_LEFT, 8, -50);

  s_progress_bar = lv_bar_create(args->screen);
  lv_obj_set_size(s_progress_bar, 308, 10);
  lv_bar_set_range(s_progress_bar, 0, 100);
  ui_theme_style_bar(s_progress_bar);
  lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -34);

  lv_obj_t *vol_row = lv_obj_create(args->screen);
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

  /* ---- state init ---- */
  s_active          = true;
  s_last_ui_ms      = 0;
  s_last_vol        = 0xFF;
  s_last_progress   = -1;
  s_last_pos_sec    = UINT32_MAX;
  s_last_dur_sec    = UINT32_MAX;
  s_last_paused     = false;
  s_last_playing    = false;
  s_last_track_index = -1;
  s_last_track_count = -1;
  s_last_track_type = AUDIO_TRACK_TYPE_NONE;
  s_last_sample_rate_hz = UINT32_MAX;
  s_last_channels = UINT16_MAX;
  s_last_bits_per_sample = UINT16_MAX;
  s_last_bitrate_kbps = UINT16_MAX;
  s_last_is_float = false;
  s_last_mp3_vbr = false;
  s_track_confirmed_playing = false;
  preview_audio_vol_hold_reset();

  preview_audio_start_track(path);

  return true;
}

/* ------------------------------------------------------------------ close */

static void preview_audio_close(void) {
  audio_stop_playback();
  /* Wait for audio task to exit to free its 32KB stack. */
  TickType_t start = xTaskGetTickCount();
  while (audio_is_playing() && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(500)) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (s_playlist) {
    if (s_playlist_from_shared)
      platform_free(s_playlist);
    else
      free(s_playlist);
    s_playlist = NULL;
  }
  s_playlist_count = 0;
  s_current_index = 0;
  s_playlist_cwd[0] = '\0';
  s_playlist_from_shared = false;
  ui_chrome_detach(&s_chrome);
  preview_audio_vol_hold_reset();
  s_active        = false;
  s_filename_label = NULL;
  s_tech_label    = NULL;
  s_track_label   = NULL;
  s_time_label    = NULL;
  s_status_label  = NULL;
  s_progress_bar  = NULL;
  s_vol_bar       = NULL;
  s_vol_pct_label = NULL;
  s_mode_label    = NULL;
}

/* ------------------------------------------------------------------ key handler */

static bool preview_audio_on_key(const input_gamepad_state *gp,
                                 const bool edge[]) {
  if (!s_active)
    return false;

  if (edge[GAMEPAD_INPUT_UP]) {
    preview_audio_apply_volume_delta(1);
    preview_audio_arm_volume_hold(1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_DOWN]) {
    preview_audio_apply_volume_delta(-1);
    preview_audio_arm_volume_hold(-1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_LEFT]) {
    audio_seek_seconds(-5);
    preview_audio_update_ui(true);
    return true;
  }
  if (edge[GAMEPAD_INPUT_RIGHT]) {
    audio_seek_seconds(5);
    preview_audio_update_ui(true);
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
  if (edge[GAMEPAD_INPUT_START] || edge[GAMEPAD_INPUT_A]) {
    bool p = audio_is_paused(), pl = audio_is_playing();
    ESP_LOGI("preview_audio", "key: START/A toggle pause p=%d playing=%d", p, pl);
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
    ui_backlight_toggle();
    if (ui_backlight_is_on())
      preview_audio_update_ui(true);
    return true;
  }

  preview_audio_vol_hold_tick(gp);
  (void)gp;
  return false;
}

/* ------------------------------------------------------------------ timer */

static void preview_audio_on_timer(void) {
  if (!s_active)
    return;

  bool playing = audio_is_playing();
  bool paused  = audio_is_paused();
  // ESP_LOGI("preview_audio", "timer: playing=%d paused=%d confirmed=%d",
  //          playing, paused, s_track_confirmed_playing);

  /* Confirm that the current track has started (guard against the brief gap
   * between audio_play_file_async() and the play task setting s_playing). */
  if (!s_track_confirmed_playing && playing)
    s_track_confirmed_playing = true;

  /* Detect natural track end and auto-advance according to play mode. */
  if (s_track_confirmed_playing && !playing && !paused) {
    ESP_LOGI("preview_audio", "timer: track-end detected, mode=%d idx=%d cnt=%d",
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

  preview_audio_update_ui(false);
}

static const char *preview_audio_current_path(void) {
  return s_active ? s_current_path : NULL;
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

#endif
