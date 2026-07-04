#include "preview_audio.h"
#include "preview_audio_lrc.h"
#include "preview_audio_playlist.h"
#include "preview_audio_tags.h"
#include "preview_audio_ui.h"

#include "audio.h"
#include "eq.h"
#include "id3.h"
#include "file_manager.h"
#include "hal_settings.h"
#include "input_bridge.h"
#include "input_repeat.h"
#include "platform_log.h"
#include "platform_time.h"
#include "ui_home.h"
#include "ui_settings.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#else
#include "esp_random.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "preview_audio";

#define PREVIEW_AUDIO_LOGI(...) platform_log(PLATFORM_LOG_INFO, TAG, __VA_ARGS__)
#define PREVIEW_AUDIO_LOGW(...) platform_log(PLATFORM_LOG_WARN, TAG, __VA_ARGS__)
#define PREVIEW_AUDIO_LOGE(...) platform_log(PLATFORM_LOG_ERROR, TAG, __VA_ARGS__)

static void preview_audio_wait_ms(uint32_t ms) {
  platform_sleep_ms(ms);
}

#define AUDIO_SEEK_STEP_SECONDS   5
#define AUDIO_ADJUST_ACCEL_EVERY  4
#define AUDIO_ADJUST_MAX_SCALE    4

static audio_playlist_t   s_pl;
static audio_ui_t         s_ui;
static audio_tag_cache_t  s_tag_cache;
static char    s_current_path[AUDIO_PATH_MAX];
static bool    s_active;
static bool    s_session_active;
static bool    s_close_to_background;
static lv_timer_t *s_session_timer;
static play_mode_t s_play_mode;
/* Set true once the current track is confirmed playing; cleared on track end
 * or when a new track is started, to gate auto-advance triggering. */
static bool s_track_confirmed_playing;
static input_repeat_state_t s_volume_repeat;
static input_repeat_state_t s_seek_repeat;

static const char *preview_audio_current_path(void);
static void preview_audio_reset_session_state(void);
static void preview_audio_stop_session_internal(void);
static void preview_audio_session_tick(bool force_ui);
static void preview_audio_session_timer_cb(lv_timer_t *timer);
static bool preview_audio_prepare_session(const char *path, char *shared_names,
                                          int shared_count, int shared_index,
                                          int shared_name_stride,
                                          const char *cwd_override);
static void preview_audio_persist_state(bool armed);
static bool preview_audio_path_exists(const char *path);
static void preview_audio_update_ui(bool force);

static const input_repeat_config_t s_audio_adjust_repeat = {
    .initial_delay_ms = 400,
    .repeat_delay_ms = 80,
    .accel_every = AUDIO_ADJUST_ACCEL_EVERY,
    .max_scale = AUDIO_ADJUST_MAX_SCALE,
};

/* ------------------------------------------------------------------ helpers */

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

static void preview_audio_persist_state(bool armed) {
  hal_settings_save(SettingMusicSessionArmed, armed ? 1 : 0);
  if (s_current_path[0] != '\0')
    hal_settings_save_str(SettingMusicSessionPath, s_current_path);
}

static void preview_audio_sync_file_manager_cwd(void) {
  if (s_pl.cwd[0] != '\0')
    fm_set_cwd(s_pl.cwd);
}

/* ------------------------------------------------------------------ can_open */

static bool preview_audio_can_open(const char *path) {
  return fm_is_playable_audio_filename(fm_base_name(path));
}

/* ------------------------------------------------------------------ UI update (glue: collects state → ui module) */

static void preview_audio_update_ui(bool force) {
  if (!s_active)
    return;

  uint32_t pos = audio_get_position_ms();
  uint32_t dur = audio_get_duration_ms();
  if (dur == 0)
    dur = 1;
  int progress = (int32_t)(pos * 100 / dur);

  char tech_buf[128] = "";
  audio_track_info_t ti;
  if (audio_get_track_info(&ti)) {
    audio_tag_format_tech(tech_buf, sizeof(tech_buf), &ti,
                           preview_audio_lrc_has_data());
  }

  audio_ui_snap_t snap = {
    .progress    = progress,
    .volume      = audio_get_volume(),
    .pos_sec     = pos / 1000,
    .dur_sec     = dur / 1000,
    .paused      = audio_is_paused(),
    .playing     = audio_is_playing(),
    .track_index = s_pl.current_index,
    .track_count = s_pl.count,
    .tech_text   = tech_buf,
    .lrc_text    = preview_audio_lrc_get_text(pos, force),
  };

  audio_ui_update(&s_ui, force, &snap);
}

/* ------------------------------------------------------------------ volume / seek */

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

  PREVIEW_AUDIO_LOGI("seek: delta=%d, pos_before=%u, expect_pos=%u",
                     clamped_delta, pos_ms, pos_ms + (uint32_t)clamped_delta * 1000);

  audio_seek_seconds(clamped_delta);
  /* Wait for audio task to process the seek and update s_position_ms.
   * audio_seek_seconds() is asynchronous — it only sets a pending flag.
   * Without this delay, the next audio_get_position_ms() returns the
   * pre-seek position, causing LRC lyrics to show the wrong line. */
  platform_sleep_ms(100);
  {
    uint32_t pos_after = audio_get_position_ms();
    PREVIEW_AUDIO_LOGI("seek: pos_after_sleep=%u (Δ=%+dms)",
                       pos_after, (int)((int64_t)pos_after - (int64_t)pos_ms));
  }
  preview_audio_lrc_seek_reset();
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
  PREVIEW_AUDIO_LOGI("start_track ENTER: path=%s", path);
  strlcpy(s_current_path, path, sizeof(s_current_path));
  s_track_confirmed_playing = false;

  /* ---- Read ID3 tags before starting audio (avoids file access race) ---- */
  const char *bn = fm_base_name(path);
  mp3_tags_t tags;
  memset(&tags, 0, sizeof(tags));
  bool has_tags = mp3_read_tags(path, &tags);

  PREVIEW_AUDIO_LOGI("title='%s' (len=%u) artist='%s' album='%s'",
                     tags.title, (unsigned)strlen(tags.title),
                     tags.artist, tags.album);

  preview_audio_ensure_session_timer();
  preview_audio_persist_state(true);
  audio_play_file_async(path);

  audio_ui_set_title(&s_ui, bn);

  /* cache ID3 tags so restore-foreground can repopulate labels */
  audio_tag_cache_from_id3(&s_tag_cache, bn, has_tags ? &tags : NULL);

  /* update card labels from tag cache */
  if (s_ui.tag_title_label && lv_obj_is_valid(s_ui.tag_title_label))
    lv_label_set_text(s_ui.tag_title_label, s_tag_cache.title);
  if (s_ui.tag_artist_label && lv_obj_is_valid(s_ui.tag_artist_label))
    lv_label_set_text(s_ui.tag_artist_label, s_tag_cache.artist);

  /* parse built-in LRC lyrics */
  if (s_ui.lyric_label && lv_obj_is_valid(s_ui.lyric_label))
    lv_label_set_text(s_ui.lyric_label, "");
  preview_audio_lrc_parse(has_tags && tags.lyrics[0] ? tags.lyrics : NULL);

  bool now_playing = audio_is_playing();
  PREVIEW_AUDIO_LOGI("start_track EXIT: now_playing=%d has_tags=%d",
                     now_playing, has_tags);
  preview_audio_update_ui(true);
}

static void preview_audio_play_index(int idx) {
  if (s_pl.count == 0)
    return;
  while (idx < 0)
    idx += s_pl.count;
  idx %= s_pl.count;
  s_pl.current_index = idx;

  char full[AUDIO_PATH_MAX];
  if (!audio_playlist_build_path(&s_pl, full, sizeof(full),
                                  s_pl.items[s_pl.current_index]))
    return;

  PREVIEW_AUDIO_LOGI("play_index idx=%d playlist=%s count=%d full=%s",
                     idx, s_pl.items[idx], s_pl.count, full);
  preview_audio_start_track(full);
}

/* ------------------------------------------------------------------ session lifecycle */

static bool preview_audio_prepare_session(const char *path, char *shared_names,
                                          int shared_count, int shared_index,
                                          int shared_name_stride,
                                          const char *cwd_override) {
  if (!path || path[0] == '\0')
    return false;

  audio_playlist_init(&s_pl);
  s_close_to_background = false;
  strlcpy(s_current_path, path, sizeof(s_current_path));

  if (!audio_playlist_build(&s_pl, cwd_override, path,
                            shared_names, shared_count,
                            shared_index, shared_name_stride)) {
    return false;
  }
  s_session_active = true;
  s_track_confirmed_playing = false;
  ui_chrome_set_music_active(true);
  return true;
}

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

  const char *title  = fm_base_name(path);
  const char *artist = "";

  if (!audio_ui_build(&s_ui, args->screen, title, artist,
                       s_play_mode, eq_preset_name(eq_get_preset())))
    return false;
  s_active = true;
  preview_audio_reset_adjust_repeats();
  preview_audio_start_track(path);

  return true;
}

static void preview_audio_reset_session_state(void) {
  audio_playlist_free(&s_pl);
  audio_ui_close(&s_ui);
  audio_tag_cache_init(&s_tag_cache);
  preview_audio_drop_session_timer();
  s_current_path[0] = '\0';
  s_active = false;
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
    audio_ui_close(&s_ui);
    s_active = false;
    preview_audio_reset_adjust_repeats();
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
      preview_audio_play_index(audio_playlist_shuffle_step(&s_pl, -1, false));
    else
      preview_audio_play_index(s_pl.current_index - 1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_R]) {
    if (s_play_mode == PLAY_MODE_SHUFFLE)
      preview_audio_play_index(audio_playlist_shuffle_step(&s_pl, 1, false));
    else
      preview_audio_play_index(s_pl.current_index + 1);
    return true;
  }
  if (edge[GAMEPAD_INPUT_START]) {
    eq_next_preset();
    hal_settings_save(SettingEqPreset, (int32_t)eq_get_preset());
    audio_ui_set_eq(&s_ui, eq_preset_name(eq_get_preset()));
    return true;
  }
  if (edge[GAMEPAD_INPUT_A]) {
    bool p = audio_is_paused(), pl = audio_is_playing();
    PREVIEW_AUDIO_LOGI("key: A toggle pause p=%d playing=%d", p, pl);
    if (!pl && !p)
      preview_audio_play_index(s_pl.current_index);
    else
      audio_toggle_pause();
    preview_audio_update_ui(true);
    return true;
  }
  if (edge[GAMEPAD_INPUT_SELECT]) {
    s_play_mode = (play_mode_t)((s_play_mode + 1) % PLAY_MODE_COUNT);
    if (s_play_mode == PLAY_MODE_SHUFFLE)
      audio_playlist_shuffle_reset(&s_pl, s_pl.current_index);
    audio_ui_set_mode(&s_ui, s_play_mode);
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
                       s_play_mode, s_pl.current_index, s_pl.count);
    s_track_confirmed_playing = false;
    switch (s_play_mode) {
      case PLAY_MODE_LIST_LOOP:
        preview_audio_play_index((s_pl.current_index + 1) % s_pl.count);
        return;
      case PLAY_MODE_SHUFFLE:
        preview_audio_play_index(audio_playlist_shuffle_step(&s_pl, 1, true));
        return;
      case PLAY_MODE_SINGLE_LOOP:
        preview_audio_play_index(s_pl.current_index);
        return;
      case PLAY_MODE_LIST_PLAY:
        if (s_pl.current_index < s_pl.count - 1) {
          preview_audio_play_index(s_pl.current_index + 1);
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

/* ------------------------------------------------------------------ public API */

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
    preview_audio_play_index(audio_playlist_shuffle_step(&s_pl, (delta > 0) ? 1 : -1, false));
  else
    preview_audio_play_index(s_pl.current_index + ((delta > 0) ? 1 : -1));
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

  const char *title  = s_tag_cache.has_tags ? s_tag_cache.title
                                            : fm_base_name(s_current_path);
  const char *artist = s_tag_cache.has_tags ? s_tag_cache.artist : "";

  if (!audio_ui_build(&s_ui, screen, title, artist,
                       s_play_mode, eq_preset_name(eq_get_preset())))
    return false;
  s_active = true;
  /* Chrome title always shows the file name (not the ID3 tag title). */
  ui_chrome_set_title(&s_ui.chrome, fm_base_name(s_current_path));
  preview_audio_reset_adjust_repeats();
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
    .id           = "audio",
    .can_open     = preview_audio_can_open,
    .open         = preview_audio_open,
    .close        = preview_audio_close,
    .on_key       = preview_audio_on_key,
    .on_timer     = preview_audio_on_timer,
    .current_path = preview_audio_current_path,
};
