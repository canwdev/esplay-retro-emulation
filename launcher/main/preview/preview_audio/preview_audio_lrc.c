#include "preview_audio_lrc.h"

#include "platform_log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "LRC";

#define LRC_LOGI(...) platform_log(PLATFORM_LOG_INFO, TAG, __VA_ARGS__)
#define LRC_LOGW(...) platform_log(PLATFORM_LOG_WARN, TAG, __VA_ARGS__)

/* ---- internal state ---- */

static int       s_count;
static uint32_t  s_times[LRC_MAX_LINES];
static uint16_t  s_text_off[LRC_MAX_LINES];
static uint16_t  s_text_len[LRC_MAX_LINES];
static char      s_raw[LRC_RAW_MAX];

static int       s_last_idx;
static uint32_t  s_last_displayed_ms;

/** Buffer for the merged display text returned by get_text. */
static char s_display_buf[512];

/* ---- word-level timestamp stripping ---- */

/** Strip <mm:ss.xx> or <mm:ss> word-level timestamps in-place.
 *  Returns the new (possibly shorter) string length. */
static size_t strip_word_timestamps(char *s) {
  char *src = s, *dst = s;
  while (*src) {
    if (*src == '<' && src[1] >= '0' && src[1] <= '9') {
      char *p   = src + 1;
      int  d    = 0; /* digit count */
      int  c    = 0; /* colon count */
      while (*p && ((*p >= '0' && *p <= '9') || *p == ':' || *p == '.')) {
        if (*p >= '0' && *p <= '9') d++;
        if (*p == ':') c++;
        p++;
      }
      if (*p == '>' && c >= 1 && d >= 2) {
        src = p + 1; /* skip entire <timestamp> tag */
        continue;
      }
    }
    *dst++ = *src++;
  }
  *dst = '\0';
  return (size_t)(dst - s);
}

/* ---- public API ---- */

void preview_audio_lrc_init(void) {
  s_count             = 0;
  s_last_idx          = -1;
  s_last_displayed_ms = UINT32_MAX;
  s_raw[0]            = '\0';
}

void preview_audio_lrc_parse(const char *lyrics) {
  s_count             = 0;
  s_last_idx          = -1;
  s_last_displayed_ms = UINT32_MAX;

  if (!lyrics || lyrics[0] == '\0')
    return;

  size_t raw_len = strlen(lyrics);

  if (raw_len >= sizeof(s_raw)) {
    LRC_LOGW("lyrics too large (%u >= %u), truncating",
             (unsigned)raw_len, (unsigned)sizeof(s_raw));
  }
  /* Copy with truncation (avoid strlcpy for MSVC compat). */
  {
    size_t n = raw_len;
    if (n >= sizeof(s_raw))
      n = sizeof(s_raw) - 1;
    memcpy(s_raw, lyrics, n);
    s_raw[n] = '\0';
  }

  /* strip UTF-8 BOM (0xEF 0xBB 0xBF) if present */
  if ((unsigned char)s_raw[0] == 0xEF &&
      (unsigned char)s_raw[1] == 0xBB &&
      (unsigned char)s_raw[2] == 0xBF) {
    memmove(s_raw, s_raw + 3, strlen(s_raw + 3) + 1);
  }

  /* strip word-level <mm:ss.xx> timestamps before parsing */
  size_t stripped_len = strip_word_timestamps(s_raw);
  (void)stripped_len;

  /* parse [mm:ss.xx] timestamps */
  char *p = s_raw;
  while (*p && s_count < LRC_MAX_LINES) {
    /* skip to next '[' */
    while (*p && *p != '[')
      p++;
    if (*p == '\0')
      break;

    char *ts_start = p + 1;
    char *ts_end   = ts_start;
    while (*ts_end && *ts_end != ']')
      ts_end++;
    if (*ts_end != ']' || ts_end == ts_start) {
      p++;
      continue;
    }

    /* parse [mm:ss.xx] or [mm:ss] */
    *ts_end = '\0';
    int min = 0, sec = 0, cs = 0;
    bool valid = false;
    char *dot  = strchr(ts_start, '.');
    if (dot) {
      *dot     = '\0';
      if (sscanf(ts_start, "%d:%d", &min, &sec) == 2) {
        cs    = atoi(dot + 1);
        valid = true;
      }
      *dot = '.';
    } else if (sscanf(ts_start, "%d:%d", &min, &sec) == 2) {
      cs    = 0;
      valid = true;
    }
    *ts_end = ']';
    if (!valid) {
      p = ts_end + 1;
      continue;
    }

    /* text after ']' until end-of-line or next '[' */
    char *text = ts_end + 1;
    while (*text == ' ')
      text++;
    char *text_end = text;
    while (*text_end && *text_end != '\r' && *text_end != '\n' &&
           *text_end != '[')
      text_end++;
    while (text_end > text && text_end[-1] == ' ')
      text_end--;

    if (text_end > text) {
      s_times[s_count]    = (uint32_t)(min * 60000 + sec * 1000 + cs * 10);
      s_text_off[s_count] = (uint16_t)(text - s_raw);
      s_text_len[s_count] = (uint16_t)(text_end - text);
      s_count++;
    }
    p = text_end;
  }

  if (s_count >= LRC_MAX_LINES)
    LRC_LOGW("hit line limit %d, some lines dropped", LRC_MAX_LINES);

  /* fallback: no timestamps found → show entire text from start */
  if (s_count == 0) {
    LRC_LOGW("no timestamps found, showing raw text as fallback");
    char *text = s_raw;
    while (*text == '\r' || *text == '\n' || *text == ' ')
      text++;
    if (*text) {
      s_times[0]    = 0;
      s_text_off[0] = (uint16_t)(text - s_raw);
      size_t tlen   = strlen(text);
      while (tlen > 0 && (text[tlen - 1] == '\r' || text[tlen - 1] == '\n' ||
                          text[tlen - 1] == ' '))
        tlen--;
      s_text_len[0] = (uint16_t)tlen;
      s_count       = 1;
    }
  }
}

bool preview_audio_lrc_has_data(void) {
  return s_count > 0;
}

/* ---- internal: find index for a position ---- */

static int find_index(uint32_t pos_ms) {
  if (s_count == 0)
    return -1;

  int idx = s_last_idx;
  if (idx < 0 || idx >= s_count)
    idx = 0;

  while (idx > 0 && s_times[idx] > pos_ms)
    idx--;
  while (idx + 1 < s_count && s_times[idx + 1] <= pos_ms)
    idx++;

  return (s_times[idx] <= pos_ms) ? idx : -1;
}

/* ---- build merged display text ---- */

static const char *build_text(int idx) {
  /* merge consecutive lines sharing the same timestamp (bilingual).
   * find_index returns the LAST index with this timestamp,
   * so scan backward to find the first. */
  int start = idx;
  while (start > 0 && s_times[start - 1] == s_times[idx])
    start--;
  int end = idx;
  while (end + 1 < s_count && s_times[end + 1] == s_times[idx])
    end++;

  int off = 0;
  for (int i = start; i <= end && off < (int)sizeof(s_display_buf) - 1; i++) {
    if (i > start)
      s_display_buf[off++] = '\n';
    uint16_t len = s_text_len[i];
    if (off + len > (int)sizeof(s_display_buf) - 1)
      len = (uint16_t)(sizeof(s_display_buf) - 1 - off);
    memcpy(s_display_buf + off, s_raw + s_text_off[i], len);
    off += len;
  }
  s_display_buf[off] = '\0';
  return s_display_buf;
}

/* ---- public: get display text ---- */

const char *preview_audio_lrc_get_text(uint32_t pos_ms, bool force) {
  if (s_count == 0)
    return force ? "" : NULL;

  int idx = find_index(pos_ms);

  if (idx < 0) {
    /* before the first timestamp */
    if (force || s_last_displayed_ms != UINT32_MAX) {
      s_last_idx          = -1;
      s_last_displayed_ms = UINT32_MAX;
      return "";
    }
    return NULL;
  }

  s_last_idx = idx;

  if (force || s_times[idx] != s_last_displayed_ms) {
    s_last_displayed_ms = s_times[idx];
    return build_text(idx);
  }

  return NULL; /* same line as last tick — no update needed */
}

void preview_audio_lrc_seek_reset(void) {
  s_last_idx          = -1;
  s_last_displayed_ms = UINT32_MAX;
}
