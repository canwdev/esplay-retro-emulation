#include "text_codec.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "text/gb2312_uni.inc"

static bool utf8_valid(const uint8_t *data, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint8_t c = data[i];
    if (c <= 0x7F) {
      i++;
      continue;
    }
    if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80)
        return false;
      i += 2;
      continue;
    }
    if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= len || (data[i + 1] & 0xC0) != 0x80 ||
          (data[i + 2] & 0xC0) != 0x80)
        return false;
      i += 3;
      continue;
    }
    if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= len || (data[i + 1] & 0xC0) != 0x80 ||
          (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80)
        return false;
      i += 4;
      continue;
    }
    return false;
  }
  return true;
}

static size_t utf8_encode(uint32_t cp, char *out) {
  if (cp <= 0x7F) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static uint32_t gb2312_to_unicode(uint8_t b1, uint8_t b2) {
  if (b1 < 0xA1 || b1 > 0xF7 || b2 < 0xA1 || b2 > 0xFE)
    return 0xFFFD;
  int idx = (b1 - 0xA1) * 94 + (b2 - 0xA1);
  uint16_t u = gb2312_uni[idx];
  return u == 0xFFFF ? 0xFFFD : u;
}

static uint32_t gbk_to_unicode(uint8_t b1, uint8_t b2) {
  if (b1 <= 0x7F)
    return b1;
  if (b1 >= 0x81 && b1 <= 0xFE && b2 >= 0x40 && b2 <= 0xFE) {
    if (b1 >= 0xA1 && b1 <= 0xF7 && b2 >= 0xA1 && b2 <= 0xFE)
      return gb2312_to_unicode(b1, b2);
    return 0xFFFD;
  }
  return 0xFFFD;
}

static char *decode_gbk(const uint8_t *raw, size_t raw_len, size_t *out_len) {
  char *out = malloc(raw_len * 3 + 4);
  if (!out)
    return NULL;

  size_t o = 0;
  for (size_t i = 0; i < raw_len;) {
    uint8_t b = raw[i];
    if (b == '\r') {
      out[o++] = '\n';
      i++;
      continue;
    }
    if (b < 0x80) {
      out[o++] = (char)b;
      i++;
      continue;
    }
    if (i + 1 >= raw_len) {
      out[o++] = '?';
      break;
    }
    uint32_t cp = gbk_to_unicode(b, raw[i + 1]);
    o += utf8_encode(cp, &out[o]);
    i += 2;
  }
  out[o] = '\0';
  *out_len = o;
  return out;
}

char *text_decode_to_utf8(const uint8_t *raw, size_t raw_len, size_t *out_len,
                            text_encoding_t *out_enc) {
  if (!raw || raw_len == 0 || !out_len)
    return NULL;

  size_t off = 0;
  if (raw_len >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
    off = 3;

  const uint8_t *body = raw + off;
  size_t body_len = raw_len - off;

  if (utf8_valid(body, body_len)) {
    char *out = malloc(body_len + 1);
    if (!out)
      return NULL;
    memcpy(out, body, body_len);
    out[body_len] = '\0';
    for (char *p = out; *p; p++) {
      if (*p == '\r')
        *p = '\n';
    }
    *out_len = body_len;
    if (out_enc)
      *out_enc = TEXT_ENC_UTF8;
    return out;
  }

  if (out_enc)
    *out_enc = TEXT_ENC_GBK;
  return decode_gbk(body, body_len, out_len);
}
