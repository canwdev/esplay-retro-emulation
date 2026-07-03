#include "preview_audio_ui.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#endif

#include "platform_log.h"
#include "ui_backlight.h"
#include "ui_chrome.h"
#include "ui_font.h"
#include "ui_theme.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_ui";

/* ---- helpers ---- */

static const char *play_mode_text(play_mode_t m) {
  switch (m) {
    case PLAY_MODE_LIST_LOOP:   return LV_SYMBOL_LOOP " All Loop";
    case PLAY_MODE_SHUFFLE:     return LV_SYMBOL_SHUFFLE " Random";
    case PLAY_MODE_SINGLE_LOOP: return LV_SYMBOL_LOOP " x1 Loop";
    case PLAY_MODE_LIST_PLAY:   return LV_SYMBOL_NEXT " Sequential";
    default: return "";
  }
}

/* ---- public API ---- */

void audio_ui_init(audio_ui_t *ui) {
  memset(ui, 0, sizeof(*ui));
}

bool audio_ui_build(audio_ui_t *ui, lv_obj_t *screen,
                    const char *title, const char *artist,
                    play_mode_t play_mode, const char *eq_name) {
  if (!ui || !screen)
    return false;

  ui_chrome_detach(&ui->chrome);
  lv_obj_clean(screen);
  ui_theme_apply_screen(screen);

  ui->chrome = ui_chrome_create(screen, "Music Player");

  lv_obj_t *card = lv_obj_create(screen);
  lv_obj_remove_style_all(card);
  ui_theme_style_panel(card);
  lv_obj_set_size(card, LV_PCT(98), 160);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, ui_chrome_body_top());
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_hor(card, 6, 0);
  lv_obj_set_style_pad_ver(card, 4, 0);
  lv_obj_set_style_pad_row(card, 2, 0);

  ui->tag_title_label = lv_label_create(card);
  lv_obj_set_width(ui->tag_title_label, LV_PCT(100));
  lv_label_set_long_mode(ui->tag_title_label, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(ui->tag_title_label, title);
  ui_theme_style_label_primary(ui->tag_title_label);

  ui->tag_artist_label = lv_label_create(card);
  lv_obj_set_width(ui->tag_artist_label, LV_PCT(100));
  lv_label_set_long_mode(ui->tag_artist_label, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(ui->tag_artist_label, artist);
  ui_theme_style_label_secondary(ui->tag_artist_label);

  /* spacer between artist and tech info */
  lv_obj_t *card_gap = lv_obj_create(card);
  lv_obj_remove_style_all(card_gap);
  lv_obj_set_height(card_gap, 6);

  ui->tech_label = lv_label_create(card);
  lv_obj_set_width(ui->tech_label, LV_PCT(100));
  lv_label_set_long_mode(ui->tech_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_label_set_text(ui->tech_label, "");
  ui_theme_style_label_secondary(ui->tech_label);

  /* lyric line (auto-wrap, hidden when empty) */
  ui->lyric_label = lv_label_create(card);
  lv_obj_set_width(ui->lyric_label, LV_PCT(100));
  lv_label_set_long_mode(ui->lyric_label, LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_text(ui->lyric_label, "");
  ui_theme_style_label_accent(ui->lyric_label);

  /* filler stretches card to fill remaining space (flex:1) */
  lv_obj_t *card_fill = lv_obj_create(card);
  lv_obj_remove_style_all(card_fill);
  lv_obj_set_flex_grow(card_fill, 1);

  /* ---- time row: pos / status / dur (space-between) ---- */
  lv_obj_t *time_row = lv_obj_create(screen);
  lv_obj_remove_style_all(time_row);
  lv_obj_set_size(time_row, LV_PCT(100), 22);
  lv_obj_align(time_row, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(time_row, 6, 0);

  ui->time_pos_label = lv_label_create(time_row);
  lv_label_set_text(ui->time_pos_label, "0:00");
  ui_theme_style_label_secondary(ui->time_pos_label);

  ui->status_label = lv_label_create(time_row);
  lv_label_set_text(ui->status_label, LV_SYMBOL_PLAY " Playing");
  ui_theme_style_label_primary(ui->status_label);

  ui->time_dur_label = lv_label_create(time_row);
  lv_label_set_text(ui->time_dur_label, "0:00");
  ui_theme_style_label_secondary(ui->time_dur_label);

  /* ---- progress bar (full width) ---- */
  ui->progress_bar = lv_bar_create(screen);
  lv_obj_set_size(ui->progress_bar, LV_PCT(96), 10);
  lv_bar_set_range(ui->progress_bar, 0, 100);
  ui_theme_style_bar(ui->progress_bar);
  lv_obj_align(ui->progress_bar, LV_ALIGN_BOTTOM_MID, 0, -22);

  /* ---- info row (justify-between via spacers, vol icon+% grouped) ---- */
  lv_obj_t *info_row = lv_obj_create(screen);
  lv_obj_remove_style_all(info_row);
  lv_obj_set_size(info_row, LV_PCT(100), 22);
  lv_obj_align(info_row, LV_ALIGN_BOTTOM_MID, 0, -0);
  lv_obj_set_flex_flow(info_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(info_row, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(info_row, 6, 0);
  lv_obj_set_style_pad_column(info_row, 4, 0);

  ui->track_label = lv_label_create(info_row);
  lv_label_set_text(ui->track_label, "1/1");
  ui_theme_style_label_secondary(ui->track_label);

  /* spacer between track and mode */
  lv_obj_t *sp0 = lv_obj_create(info_row);
  lv_obj_remove_style_all(sp0);
  lv_obj_set_flex_grow(sp0, 1);
  lv_obj_set_height(sp0, 1);

  ui->mode_label = lv_label_create(info_row);
  lv_label_set_text(ui->mode_label, play_mode_text(play_mode));
  ui_theme_style_label_secondary(ui->mode_label);

  /* spacer between mode and eq */
  lv_obj_t *sp1 = lv_obj_create(info_row);
  lv_obj_remove_style_all(sp1);
  lv_obj_set_flex_grow(sp1, 1);
  lv_obj_set_height(sp1, 1);

  ui->eq_label = lv_label_create(info_row);
  lv_label_set_text(ui->eq_label, eq_name);
  ui_theme_style_label_accent(ui->eq_label);

  /* spacer between eq and volume group */
  lv_obj_t *sp2 = lv_obj_create(info_row);
  lv_obj_remove_style_all(sp2);
  lv_obj_set_flex_grow(sp2, 1);
  lv_obj_set_height(sp2, 1);

  /* volume group: icon + % together */
  lv_obj_t *vol_group = lv_obj_create(info_row);
  lv_obj_set_size(vol_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(vol_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol_group, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(vol_group, 0, 0);
  lv_obj_set_style_pad_column(vol_group, 2, 0);
  lv_obj_set_style_border_width(vol_group, 0, 0);
  lv_obj_set_style_bg_opa(vol_group, LV_OPA_TRANSP, 0);

  lv_obj_t *vol_icon = lv_label_create(vol_group);
  lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_font(vol_icon, ui_font_builtin(), 0);
  ui_theme_style_label_accent(vol_icon);

  ui->vol_pct_label = lv_label_create(vol_group);
  lv_label_set_text(ui->vol_pct_label, "50%");
  ui_theme_style_label_secondary(ui->vol_pct_label);

  /* initialise dedup caches */
  ui->last_ui_ms          = 0;
  ui->last_vol            = 0xFF;
  ui->last_progress       = -1;
  ui->last_pos_sec        = UINT32_MAX;
  ui->last_dur_sec        = UINT32_MAX;
  ui->last_paused         = false;
  ui->last_playing        = false;
  ui->last_track_index    = -1;
  ui->last_track_count    = -1;
  ui->last_tech_text[0]   = '\0';

  return true;
}

/* ---- update ---- */

void audio_ui_update(audio_ui_t *ui, bool force, const audio_ui_snap_t *snap) {
  if (!ui || !snap)
    return;
  /* No point updating widgets the user cannot see. */
  if (!ui_backlight_is_on())
    return;
  if (!ui->progress_bar || !ui->time_pos_label || !ui->status_label)
    return;
  if (!lv_obj_is_valid(ui->progress_bar) ||
      !lv_obj_is_valid(ui->time_pos_label) || !lv_obj_is_valid(ui->status_label))
    return;

  if (!force) {
    uint32_t now = lv_tick_get();
    if ((now - ui->last_ui_ms) < 250)
      return;
    ui->last_ui_ms = now;
  } else {
    ui->last_ui_ms = lv_tick_get();
  }

  /* progress bar */
  if (force || snap->progress != ui->last_progress) {
    lv_bar_set_value(ui->progress_bar, snap->progress, LV_ANIM_OFF);
    ui->last_progress = snap->progress;
  }

  /* volume */
  if (force || snap->volume != ui->last_vol) {
    ui->last_vol = snap->volume;
    if (ui->vol_pct_label && lv_obj_is_valid(ui->vol_pct_label))
      lv_label_set_text_fmt(ui->vol_pct_label, "%u%%", snap->volume);
  }

  /* time pos */
  if (force || snap->pos_sec != ui->last_pos_sec) {
    if (ui->time_pos_label && lv_obj_is_valid(ui->time_pos_label))
      lv_label_set_text_fmt(ui->time_pos_label, "%lu:%02lu",
                            (unsigned long)(snap->pos_sec / 60),
                            (unsigned long)(snap->pos_sec % 60));
    ui->last_pos_sec = snap->pos_sec;
  }

  /* time dur */
  if (force || snap->dur_sec != ui->last_dur_sec) {
    if (ui->time_dur_label && lv_obj_is_valid(ui->time_dur_label))
      lv_label_set_text_fmt(ui->time_dur_label, "%lu:%02lu",
                            (unsigned long)(snap->dur_sec / 60),
                            (unsigned long)(snap->dur_sec % 60));
    ui->last_dur_sec = snap->dur_sec;
  }

  /* track "3/12" */
  if (ui->track_label && lv_obj_is_valid(ui->track_label) &&
      snap->track_count > 0) {
    if (force || snap->track_index != ui->last_track_index ||
        snap->track_count != ui->last_track_count) {
      lv_label_set_text_fmt(ui->track_label, "%d/%d",
                            snap->track_index + 1, snap->track_count);
      ui->last_track_index = snap->track_index;
      ui->last_track_count = snap->track_count;
    }
  }

  /* status: Playing / Paused / Stopped */
  if (force || snap->paused != ui->last_paused ||
      snap->playing != ui->last_playing) {
    if (snap->paused)
      lv_label_set_text(ui->status_label, LV_SYMBOL_PAUSE " Paused");
    else if (snap->playing)
      lv_label_set_text(ui->status_label, LV_SYMBOL_PLAY " Playing");
    else
      lv_label_set_text(ui->status_label, LV_SYMBOL_STOP " Stopped");
    ui->last_paused  = snap->paused;
    ui->last_playing = snap->playing;
  }

  /* tech label (codec, sample rate, bitrate…) — dedup by string */
  if (snap->tech_text && snap->tech_text[0] &&
      ui->tech_label && lv_obj_is_valid(ui->tech_label)) {
    if (force || strcmp(snap->tech_text, ui->last_tech_text) != 0) {
      strlcpy(ui->last_tech_text, snap->tech_text,
              sizeof(ui->last_tech_text));
      lv_label_set_text(ui->tech_label, snap->tech_text);
    }
  }

  /* lyric line */
  if (snap->lrc_text && ui->lyric_label &&
      lv_obj_is_valid(ui->lyric_label))
    lv_label_set_text(ui->lyric_label, snap->lrc_text);
}

/* ---- close ---- */

void audio_ui_close(audio_ui_t *ui) {
  if (!ui)
    return;
  ui_chrome_detach(&ui->chrome);
  ui->tag_title_label  = NULL;
  ui->tag_artist_label = NULL;
  ui->tech_label       = NULL;
  ui->track_label      = NULL;
  ui->time_pos_label   = NULL;
  ui->time_dur_label   = NULL;
  ui->status_label     = NULL;
  ui->progress_bar     = NULL;
  ui->vol_pct_label    = NULL;
  ui->mode_label       = NULL;
  ui->eq_label         = NULL;
  ui->lyric_label      = NULL;
}

/* ---- setters ---- */

void audio_ui_set_title(audio_ui_t *ui, const char *text) {
  if (!ui || !text)
    return;
  ui_chrome_set_title(&ui->chrome, text);
  if (ui->tag_title_label && lv_obj_is_valid(ui->tag_title_label))
    lv_label_set_text(ui->tag_title_label, text);
}

void audio_ui_set_mode(audio_ui_t *ui, play_mode_t mode) {
  if (!ui)
    return;
  if (ui->mode_label && lv_obj_is_valid(ui->mode_label))
    lv_label_set_text(ui->mode_label, play_mode_text(mode));
}

void audio_ui_set_eq(audio_ui_t *ui, const char *name) {
  if (!ui || !name)
    return;
  if (ui->eq_label && lv_obj_is_valid(ui->eq_label))
    lv_label_set_text(ui->eq_label, name);
}
