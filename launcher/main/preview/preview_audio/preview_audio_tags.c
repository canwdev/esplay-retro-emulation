#include "preview_audio_tags.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#endif

#include <stdio.h>
#include <string.h>

/* ---- public API ---- */

void audio_tag_cache_init(audio_tag_cache_t *cache) {
  cache->title[0]   = '\0';
  cache->artist[0]  = '\0';
  cache->has_tags    = false;
}

void audio_tag_cache_from_id3(audio_tag_cache_t *cache,
                               const char *fallback_name,
                               const mp3_tags_t *tags) {
  if (tags && tags->title[0]) {
    strlcpy(cache->title, tags->title, sizeof(cache->title));
  } else {
    strlcpy(cache->title,
            fallback_name ? fallback_name : "",
            sizeof(cache->title));
  }

  cache->artist[0] = '\0';
  if (tags) {
    if (tags->artist[0] && tags->album[0])
      snprintf(cache->artist, sizeof(cache->artist), "%s - %s",
               tags->artist, tags->album);
    else if (tags->artist[0])
      strlcpy(cache->artist, tags->artist, sizeof(cache->artist));
    else if (tags->album[0])
      strlcpy(cache->artist, tags->album, sizeof(cache->artist));
  }

  cache->has_tags = true;
}

int audio_tag_format_tech(char *buf, size_t buf_sz,
                           const audio_track_info_t *ti,
                           bool has_lrc) {
  if (!buf || buf_sz == 0)
    return 0;

  buf[0] = '\0';

  if (!ti || ti->sample_rate_hz == 0) {
    strlcpy(buf, "Track info not yet available (stopped / not playing)",
            buf_sz);
    return (int)strlen(buf);
  }

  uint32_t sr10 = ti->sample_rate_hz / 100;
  uint32_t sr_i = sr10 / 10;
  uint32_t sr_f = sr10 % 10;

  if (ti->type == AUDIO_TRACK_TYPE_MP3) {
    if (ti->bitrate_kbps > 0 && ti->mp3_vbr)
      snprintf(buf, buf_sz, "MP3 %lu.%lukHz %ukbps VBR %uch",
               (unsigned long)sr_i, (unsigned long)sr_f,
               (unsigned)ti->bitrate_kbps,
               (unsigned)ti->channels);
    else if (ti->bitrate_kbps > 0)
      snprintf(buf, buf_sz, "MP3 %lu.%lukHz %ukbps %uch",
               (unsigned long)sr_i, (unsigned long)sr_f,
               (unsigned)ti->bitrate_kbps,
               (unsigned)ti->channels);
    else
      snprintf(buf, buf_sz, "MP3 %lu.%lukHz %uch",
               (unsigned long)sr_i, (unsigned long)sr_f,
               (unsigned)ti->channels);
  } else if (ti->type == AUDIO_TRACK_TYPE_WAV) {
    if (ti->is_float && ti->bits_per_sample > 0)
      snprintf(buf, buf_sz, "WAV %lu.%lukHz F%u %uch",
               (unsigned long)sr_i, (unsigned long)sr_f,
               (unsigned)ti->bits_per_sample,
               (unsigned)ti->channels);
    else if (ti->bits_per_sample > 0)
      snprintf(buf, buf_sz, "WAV %lu.%lukHz %ub %uch",
               (unsigned long)sr_i, (unsigned long)sr_f,
               (unsigned)ti->bits_per_sample,
               (unsigned)ti->channels);
    else
      snprintf(buf, buf_sz, "WAV %lu.%lukHz %uch",
               (unsigned long)sr_i, (unsigned long)sr_f,
               (unsigned)ti->channels);
  } else {
    strlcpy(buf, "N/A", buf_sz);
  }

  /* Append " LRC" marker when embedded / unsynced lyrics are present */
  if (has_lrc && buf[0] && !strstr(buf, "LRC")) {
    size_t len = strlen(buf);
    if (len + 4 < buf_sz)
      memcpy(buf + len, " LRC", 5); /* includes NUL */
  }

  return (int)strlen(buf);
}
