#ifndef PREVIEW_AUDIO_TAGS_H
#define PREVIEW_AUDIO_TAGS_H

#include "audio.h"
#include "id3.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAG_TITLE_MAX  128
#define TAG_ARTIST_MAX 260  /* 128+3+128+1 for "Artist - Album" */

typedef struct {
  char title[TAG_TITLE_MAX];
  char artist[TAG_ARTIST_MAX];
  bool has_tags;
} audio_tag_cache_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise tag cache to empty. */
void audio_tag_cache_init(audio_tag_cache_t *cache);

/**
 * Populate tag cache from ID3 tags.
 *
 * @param cache          Tag cache to fill.
 * @param fallback_name  Used as title when tags have no title (typically
 *                       the file base name).
 * @param tags           Parsed ID3 tags; pass NULL when no tags are available.
 */
void audio_tag_cache_from_id3(audio_tag_cache_t *cache,
                               const char *fallback_name,
                               const mp3_tags_t *tags);

/**
 * Format a human-readable tech-info string (codec, sample rate, bitrate…).
 *
 * Writes into @a buf and returns the number of characters written
 * (excluding the NUL terminator).  When @a ti is NULL or has
 * sample_rate_hz == 0, a placeholder string is written instead.
 *
 * @param has_lrc  When true, " LRC" is appended to the formatted string.
 */
int audio_tag_format_tech(char *buf, size_t buf_sz,
                           const audio_track_info_t *ti,
                           bool has_lrc);

#ifdef __cplusplus
}
#endif

#endif /* PREVIEW_AUDIO_TAGS_H */
