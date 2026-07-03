#ifndef PREVIEW_AUDIO_UI_H
#define PREVIEW_AUDIO_UI_H

#include "preview_audio_playlist.h" /* for play_mode_t */
#include "ui_chrome.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ state */

typedef struct {
  ui_chrome_t chrome;
  lv_obj_t *tag_title_label;
  lv_obj_t *tag_artist_label;
  lv_obj_t *tech_label;
  lv_obj_t *track_label;
  lv_obj_t *time_pos_label;
  lv_obj_t *time_dur_label;
  lv_obj_t *status_label;
  lv_obj_t *progress_bar;
  lv_obj_t *vol_pct_label;
  lv_obj_t *eq_label;
  lv_obj_t *mode_label;
  lv_obj_t *lyric_label;

  /* dedup caches */
  uint32_t last_ui_ms;
  uint8_t  last_vol;
  int      last_progress;
  uint32_t last_pos_sec;
  uint32_t last_dur_sec;
  bool     last_paused;
  bool     last_playing;
  int      last_track_index;
  int      last_track_count;
  char     last_tech_text[128];
} audio_ui_t;

/* Snapshot of playback state passed to audio_ui_update. */
typedef struct {
  int         progress;     /* 0..100 */
  uint8_t     volume;       /* 0..100 */
  uint32_t    pos_sec;
  uint32_t    dur_sec;
  bool        paused;
  bool        playing;
  int         track_index;  /* 0-based */
  int         track_count;
  const char *tech_text;    /* formatted by tags module, NULL to skip */
  const char *lrc_text;     /* from LRC module, NULL to skip */
} audio_ui_snap_t;

/* ------------------------------------------------------------------ API */

void audio_ui_init(audio_ui_t *ui);

/**
 * Build the full music player UI on @a screen.
 *
 * @param title   Initial title text (file base name or cached tag title).
 * @param artist  Initial artist text (tag artist/album or "").
 */
bool audio_ui_build(audio_ui_t *ui, lv_obj_t *screen,
                    const char *title, const char *artist,
                    play_mode_t play_mode, const char *eq_name);

/** Refresh all dynamic widgets.  Pass @a force to skip the 250 ms throttle. */
void audio_ui_update(audio_ui_t *ui, bool force, const audio_ui_snap_t *snap);

/** Detach chrome and null-out all widget pointers. */
void audio_ui_close(audio_ui_t *ui);

/** Update the chrome title and top-of-card title label. */
void audio_ui_set_title(audio_ui_t *ui, const char *text);

/** Update the mode label when the play mode changes. */
void audio_ui_set_mode(audio_ui_t *ui, play_mode_t mode);

/** Update the EQ label when the preset changes. */
void audio_ui_set_eq(audio_ui_t *ui, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PREVIEW_AUDIO_UI_H */
