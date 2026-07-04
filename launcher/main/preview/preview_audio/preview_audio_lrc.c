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

/* ---- timestamp parsing helper ---- */

/** Parse [mm:ss.xx] or [mm:ss] timestamp to milliseconds.
 *  Returns true on success, false on invalid format. */
static bool parse_timestamp_ms(const char *ts, uint32_t *out_ms) {
  int min = 0, sec = 0, cs = 0;

  if (strchr(ts, '.')) {
    /* Format: [mm:ss.xx] - cs is centiseconds (0-99) */
    if (sscanf(ts, "%d:%d.%d", &min, &sec, &cs) != 3)
      return false;
    /* Convert centiseconds to milliseconds with proper rounding */
    *out_ms = min * 60000 + sec * 1000 + (cs * 100) / 10;
  } else {
    /* Format: [mm:ss] */
    if (sscanf(ts, "%d:%d", &min, &sec) != 2)
      return false;
    *out_ms = min * 60000 + sec * 1000;
  }

  return true;
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
    LRC_LOGW("lyrics too large (%u >= %u), truncating — text may be incomplete",
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

  /* Strip UTF-8 BOM (0xEF 0xBB 0xBF) if present */
  if ((unsigned char)s_raw[0] == 0xEF &&
      (unsigned char)s_raw[1] == 0xBB &&
      (unsigned char)s_raw[2] == 0xBF) {
    memmove(s_raw, s_raw + 3, strlen(s_raw + 3) + 1);
  }

  /* Strip word-level <mm:ss.xx> timestamps before parsing */
  strip_word_timestamps(s_raw);

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

    /* parse [mm:ss.xx] or [mm:ss] timestamp */
    *ts_end = '\0';
    uint32_t ts_ms;
    if (!parse_timestamp_ms(ts_start, &ts_ms)) {
      *ts_end = ']';
      p = ts_end + 1;
      continue;
    }
    *ts_end = ']';

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
      s_times[s_count]    = ts_ms;
      s_text_off[s_count] = (uint16_t)(text - s_raw);
      s_text_len[s_count] = (uint16_t)(text_end - text);
      s_count++;
    }
    p = text_end;
  }

  if (s_count >= LRC_MAX_LINES)
    LRC_LOGI("parsed %d lines (hit max limit)", LRC_MAX_LINES);
  else if (s_count > 0)
    LRC_LOGI("parsed %d lines", s_count);

  /* Fallback: no timestamps found → show entire text from start */
  if (s_count == 0) {
    LRC_LOGW("no timestamps found, showing raw text as fallback");
    char *text = s_raw;
    while (*text == '\r' || *text == '\n' || *text == ' ')
      text++;
    if (*text) {
      s_times[0]    = 0;
      s_text_off[0] = (uint16_t)(text - s_raw);
      /* text_end >= text guaranteed after trimming */
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

/** Adaptive search strategy:
 *  - Linear scan: if within 3 seconds of last position (most common case)
 *  - Binary search: for distant jumps (seek, large gaps)
 *  Returns -1 if pos_ms is before the first timestamp. */
static int find_index(uint32_t pos_ms) {
  if (s_count == 0)
    return -1;

  int last_idx = s_last_idx;
  if (last_idx < 0 || last_idx >= s_count)
    last_idx = 0;

  /* Adaptive threshold: use linear scan if within 3 seconds, otherwise binary search.
   * LRC_MAX_LINES is 128, so binary search is at most 7 comparisons. */
  const uint32_t ADAPTIVE_RANGE_MS = 3000;
  uint32_t last_ts = s_times[last_idx];
  uint32_t diff = (pos_ms >= last_ts) ? (pos_ms - last_ts) : (last_ts - pos_ms);
  if (diff <= ADAPTIVE_RANGE_MS) {
    /* Linear scan: backward then forward */
    while (last_idx > 0 && s_times[last_idx] > pos_ms)
      last_idx--;
    while (last_idx + 1 < s_count && s_times[last_idx + 1] <= pos_ms)
      last_idx++;
    return (s_times[last_idx] <= pos_ms) ? last_idx : -1;
  }

  /* Binary search for distant positions */
  int lo = 0, hi = s_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (s_times[mid] <= pos_ms)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return hi;
}

/* ---- build merged display text ---- */

/** Build display text by merging consecutive lines with the same timestamp.
 *  For bilingual lyrics, multiple lines at the same timestamp are merged
 *  with '\n' separators (e.g., original + translation).
 *  @param idx Index returned by find_index (last line with this timestamp)
 *  @return Pointer to s_display_buf, valid until next call */
static const char *build_text(int idx) {
  /* Scan backward/forward to find all lines with the same timestamp */
  int start = idx;
  while (start > 0 && s_times[start - 1] == s_times[idx])
    start--;
  int end = idx;
  while (end + 1 < s_count && s_times[end + 1] == s_times[idx])
    end++;

  const int buf_size = (int)sizeof(s_display_buf);
  int off = 0;
  for (int i = start; i <= end && off < buf_size - 1; i++) {
    if (i > start)
      s_display_buf[off++] = '\n';
    uint16_t len = s_text_len[i];
    if (off + len > buf_size - 1) {
      len = (uint16_t)(buf_size - 1 - off);
      LRC_LOGW("display text truncated, buffer too small");
    }
    memcpy(s_display_buf + off, s_raw + s_text_off[i], len);
    off += len;
  }
  s_display_buf[off] = '\0';
  return s_display_buf;
}

/* ---- public: get display text ---- */

const char *preview_audio_lrc_get_text(uint32_t pos_ms, bool force) {
  if (s_count == 0)
    return "";

  int prev_idx  = s_last_idx;
  int idx       = find_index(pos_ms);

  if (idx < 0) {
    /* Before the first timestamp */
    s_last_idx          = -1;
    s_last_displayed_ms = UINT32_MAX;
    return "";
  }

  s_last_idx     = idx;
  uint32_t cur_ts  = s_times[idx];
  uint32_t next_ts = (idx + 1 < s_count) ? s_times[idx + 1] : 0;
  int      delta   = (int)((int64_t)pos_ms - (int64_t)cur_ts);
  bool     changed = (force || cur_ts != s_last_displayed_ms);

  if (changed)
    s_last_displayed_ms = cur_ts;

  const char *text = build_text(idx);

#ifdef LRC_DEBUG_LOG
  if (changed)
    LRC_LOGI("pos=%5u | %2d->%2d | cur=%5u next=%5u Δ=%+4d | %c | %s",
             pos_ms, prev_idx, idx, cur_ts, next_ts, delta,
             force ? 'F' : 'C',
             text);
#endif

  return text;
}

void preview_audio_lrc_seek_reset(void) {
#ifdef LRC_DEBUG_LOG
  LRC_LOGI("seek_reset");
#endif
  s_last_idx          = -1;
  s_last_displayed_ms = UINT32_MAX;
}
