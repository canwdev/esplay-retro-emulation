#ifndef PREVIEW_AUDIO_LRC_H
#define PREVIEW_AUDIO_LRC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of timestamped lyric lines. */
#define LRC_MAX_LINES 128

/** Maximum raw LRC text size (UTF-8 bytes). */
#define LRC_RAW_MAX 8192

/** Initialise / reset all LRC state. */
void preview_audio_lrc_init(void);

/** Parse LRC lyrics text; pass NULL or "" to clear. */
void preview_audio_lrc_parse(const char *lyrics);

/** True when at least one timestamped line has been parsed. */
bool preview_audio_lrc_has_data(void);

/**
 * Get the lyric text that should be displayed at @a pos_ms.
 *
 * When @a force is true the return value is never NULL (an empty string
 * is returned before the first line).  When @a force is false the call
 * is treated as a periodic tick: repeated calls for the same line are
 * de-duplicated and return NULL.
 *
 * The returned pointer is valid until the next call into this module.
 */
const char *preview_audio_lrc_get_text(uint32_t pos_ms, bool force);

/**
 * Signal that a seek happened so the next @c get_text call (even with
 * @a force == false) will always deliver the current lyric.
 */
void preview_audio_lrc_seek_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PREVIEW_AUDIO_LRC_H */
