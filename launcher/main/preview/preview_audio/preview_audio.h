#ifndef PREVIEW_AUDIO_H
#define PREVIEW_AUDIO_H

#include "preview.h"

extern const preview_app_t preview_audio_app;
bool preview_audio_session_is_active(void);
bool preview_audio_session_is_playback_live(void);
const char *preview_audio_session_current_path(void);
bool preview_audio_session_switch_track(int delta);
void preview_audio_restore_persisted_session(void);
bool preview_audio_restore_foreground(lv_obj_t *screen, lv_group_t *input_group);
void preview_audio_stop_session(void);

#endif
