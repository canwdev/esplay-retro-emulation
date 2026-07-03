/**
 * @file id3.h
 * @brief Lightweight ID3v2 / ID3v1 tag reader for MP3 files.
 *
 * All text frames are converted to UTF-8 regardless of source encoding
 * (ISO-8859-1, UTF-16 BOM, UTF-16BE, UTF-8), so the caller always
 * receives printable UTF-8 strings suitable for LVGL labels.
 */

#ifndef ID3_H
#define ID3_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char title[128];
  char artist[128];
  char album[128];
  char lyrics[4096];  /* USLT / ULT frame text, may contain LRC timestamps */
} mp3_tags_t;

/**
 * Read ID3 tags from an MP3 file at @a path.
 * Tries ID3v2 first, falls back to ID3v1.
 *
 * @return true if at least one tag field was populated.
 */
bool mp3_read_tags(const char *path, mp3_tags_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ID3_H */
