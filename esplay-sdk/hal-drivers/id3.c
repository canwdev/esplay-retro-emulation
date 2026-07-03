/**
 * @file id3.c
 * @brief Lightweight ID3v2 + ID3v1 parser with UTF-8 output.
 *
 * Supports ID3v2.2 / v2.3 / v2.4 text frames (TIT2, TPE1, TALB) and
 * ID3v1 trailers.  All source encodings are converted to UTF-8.
 */

#include "id3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- platform fopen (UTF-8 paths on Windows) ------------------------- */
#ifdef _WIN32
#include <windows.h>
static FILE *id3_fopen(const char *path, const char *mode) {
  wchar_t wpath[1024];
  wchar_t wmode[32];
  if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) <= 0)
    return NULL;
  if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 32) <= 0)
    return NULL;
  return _wfopen(wpath, wmode);
}
#else
#define id3_fopen(path, mode) fopen(path, mode)
#endif

/* ------------------------------------------------------------------ helpers */

static uint32_t synchsafe_u32(const uint8_t *p) {
  return ((uint32_t)(p[0] & 0x7Fu) << 21) | ((uint32_t)(p[1] & 0x7Fu) << 14) |
         ((uint32_t)(p[2] & 0x7Fu) << 7) | ((uint32_t)(p[3] & 0x7Fu));
}

static uint32_t be_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | ((uint32_t)p[3]);
}

/* ---- Latin-1 (ISO-8859-1) → UTF-8 ------------------------------------ */
static size_t latin1_to_utf8(const uint8_t *src, size_t len, char *dst,
                             size_t dst_sz) {
  size_t w = 0;
  for (size_t i = 0; i < len && w + 2 < dst_sz; i++) {
    uint8_t c = src[i];
    if (c == 0)
      continue;
    if (c < 0x80) {
      dst[w++] = (char)c;
    } else {
      dst[w++] = (char)(0xC0 | (c >> 6));
      dst[w++] = (char)(0x80 | (c & 0x3F));
    }
  }
  if (w < dst_sz)
    dst[w] = '\0';
  return w;
}

/* ---- UTF-16BE / UTF-16-BOM → UTF-8 ----------------------------------- */
static size_t utf16_to_utf8(const uint8_t *src, size_t len, uint8_t enc,
                            char *dst, size_t dst_sz) {
  size_t w = 0;
  if (len < 2)
    return 0;

  bool le = false;
  size_t off = 0;

  if (enc == 0x01 && len >= 2) {
    /* UTF-16 with BOM */
    if (src[0] == 0xFF && src[1] == 0xFE) {
      le = true;
      off = 2;
    } else if (src[0] == 0xFE && src[1] == 0xFF) {
      le = false;
      off = 2;
    } else {
      le = false; /* no BOM, assume BE */
    }
  }

  while (off + 1 < len && w + 4 < dst_sz) {
    uint32_t cp;
    uint16_t u = le ? (uint16_t)(src[off] | (src[off + 1] << 8))
                    : (uint16_t)((src[off] << 8) | src[off + 1]);
    off += 2;

    /* surrogate pair */
    if (u >= 0xD800 && u <= 0xDBFF && off + 1 < len) {
      uint16_t lo = le ? (uint16_t)(src[off] | (src[off + 1] << 8))
                       : (uint16_t)((src[off] << 8) | src[off + 1]);
      off += 2;
      if (lo >= 0xDC00 && lo <= 0xDFFF)
        cp = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
      else
        cp = u;
    } else {
      cp = u;
    }

    /* encode codepoint → UTF-8 */
    if (cp < 0x80) {
      dst[w++] = (char)cp;
    } else if (cp < 0x800) {
      dst[w++] = (char)(0xC0 | (cp >> 6));
      dst[w++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      dst[w++] = (char)(0xE0 | (cp >> 12));
      dst[w++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      dst[w++] = (char)(0x80 | (cp & 0x3F));
    } else {
      dst[w++] = (char)(0xF0 | (cp >> 18));
      dst[w++] = (char)(0x80 | ((cp >> 12) & 0x3F));
      dst[w++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      dst[w++] = (char)(0x80 | (cp & 0x3F));
    }
  }

  if (w < dst_sz)
    dst[w] = '\0';
  return w;
}

/* ---- decode a text-frame body ---------------------------------------- */
static void decode_text(const uint8_t *data, size_t len, char *out,
                        size_t out_sz) {
  if (len < 1 || out_sz < 2)
    return;
  out[0] = '\0';

  uint8_t enc    = data[0];
  const uint8_t *text = data + 1;
  size_t tlen = len - 1;

  /* trim trailing nulls */
  while (tlen > 0 && text[tlen - 1] == 0)
    tlen--;
  if (tlen == 0)
    return;

  switch (enc) {
  case 0x03: /* UTF-8 */
    if (tlen > out_sz - 1)
      tlen = out_sz - 1;
    memcpy(out, text, tlen);
    out[tlen] = '\0';
    break;
  case 0x00: /* ISO-8859-1 */
    latin1_to_utf8(text, tlen, out, out_sz);
    break;
  case 0x01: /* UTF-16 with BOM */
  case 0x02: /* UTF-16BE */
    utf16_to_utf8(text, tlen, enc, out, out_sz);
    break;
  default:
    break;
  }
}

/* ---- ID3v1 field extractor (fixed-width Latin-1, space-padded) ------ */
static void id3v1_field(const uint8_t *src, size_t len, char *dst,
                        size_t dst_sz) {
  /* find last non-space character */
  size_t end = len;
  while (end > 0 && (src[end - 1] == ' ' || src[end - 1] == 0))
    end--;
  latin1_to_utf8(src, end, dst, dst_sz);
}

/* ---- ID3v1 tag ------------------------------------------------------- */
static bool read_id3v1(FILE *f, mp3_tags_t *out) {
  uint8_t buf[128];
  if (fseek(f, -128, SEEK_END) != 0)
    return false;
  if (fread(buf, 1, 128, f) != 128)
    return false;
  if (memcmp(buf, "TAG", 3) != 0)
    return false;

  id3v1_field(buf + 3, 30, out->title, sizeof(out->title));
  id3v1_field(buf + 33, 30, out->artist, sizeof(out->artist));
  id3v1_field(buf + 63, 30, out->album, sizeof(out->album));
  return true;
}

/* ---- ID3v2 tag ------------------------------------------------------- */
static bool read_id3v2(FILE *f, mp3_tags_t *out) {
  uint8_t hdr[10];
  if (fread(hdr, 1, 10, f) != 10)
    return false;
  if (memcmp(hdr, "ID3", 3) != 0)
    return false;

  uint8_t  ver      = hdr[3]; /* major version (3 or 4 typically) */
  uint8_t  flags    = hdr[5];
  uint32_t tag_size = synchsafe_u32(hdr + 6);
  bool     found    = false;
  size_t   offset   = 10;

  /* skip extended header if present */
  if (flags & 0x40) {
    uint8_t ext[4];
    if (fread(ext, 1, 4, f) != 4)
      return false;
    uint32_t ext_sz = synchsafe_u32(ext);
    if (ext_sz >= 4 && fseek(f, (long)(ext_sz - 4), SEEK_CUR) == 0)
      offset += ext_sz;
    else
      return false;
  }

  size_t end = 10 + tag_size;
  uint8_t fhdr[10];

  while (offset < end) {
    if (fread(fhdr, 1, 10, f) != 10)
      break;
    offset += 10;

    /* padding reached */
    if (fhdr[0] == 0)
      break;

    uint32_t fsize = (ver >= 4) ? synchsafe_u32(fhdr + 4) : be_u32(fhdr + 4);
    /* allow larger frames for USLT (embedded lyrics can exceed 4 KB) */
    uint32_t max_fsize = (memcmp(fhdr, "USLT", 4) == 0 ||
                          memcmp(fhdr, "ULT", 3) == 0) ? 16384u : 4096u;
    if (fsize == 0 || fsize > max_fsize || offset + fsize > end) {
      /* skip remainder, padding likely */
      break;
    }

    /* which frame are we interested in? */
    char *dest = NULL;
    size_t dest_sz = 0;

    if (memcmp(fhdr, "TIT2", 4) == 0 || memcmp(fhdr, "TT2", 3) == 0) {
      dest = out->title;
      dest_sz = sizeof(out->title);
    } else if (memcmp(fhdr, "TPE1", 4) == 0 ||
               memcmp(fhdr, "TP1", 3) == 0) {
      dest = out->artist;
      dest_sz = sizeof(out->artist);
    } else if (memcmp(fhdr, "TALB", 4) == 0 ||
               memcmp(fhdr, "TAL", 3) == 0) {
      dest = out->album;
      dest_sz = sizeof(out->album);
    } else if (memcmp(fhdr, "USLT", 4) == 0 ||
               memcmp(fhdr, "ULT", 3) == 0) {
      dest = out->lyrics;
      dest_sz = sizeof(out->lyrics);
    }

    if (dest && dest_sz) {
      uint8_t *buf = malloc(fsize);
      if (buf && fread(buf, 1, fsize, f) == fsize) {
        if (dest == out->lyrics) {
          /* USLT frame: encoding(1) + language(3) +
           * content_descriptor(0-term) + lyrics_text */
          /* printf("[id3] USLT frame fsize=%lu enc=%u\n", */
          /*        (unsigned long)fsize, (unsigned)buf[0]); */
          uint8_t enc   = buf[0];
          size_t  off   = 4; /* skip encoding + language */
          if (enc == 0x01 || enc == 0x02) {
            /* UTF-16: 2-byte null terminator */
            while (off + 1 < fsize) {
              if (buf[off] == 0 && buf[off + 1] == 0) {
                off += 2;
                break;
              }
              off += 2;
            }
          } else {
            /* Latin-1 / UTF-8: 1-byte null terminator */
            while (off < fsize && buf[off] != 0)
              off++;
            if (off < fsize)
              off++;
          }
          if (off < fsize) {
            /* reconstruct [enc][lyrics_text] for decode_text() */
            size_t   text_len = fsize - off;
            uint8_t *tmp = malloc(text_len + 1);
            if (tmp) {
              tmp[0] = enc;
              memcpy(tmp + 1, buf + off, text_len);
              decode_text(tmp, text_len + 1, dest, dest_sz);
              free(tmp);
              found = true;
              /* printf("[id3] USLT lyrics decoded, len=%u\n", */
              /*        (unsigned)strlen(dest)); */
            } else {
              printf("[id3] USLT malloc failed for tmp buf\n");
            }
          } else {
            printf("[id3] USLT no text after descriptor (off=%u >= fsize=%lu)\n",
                   (unsigned)off, (unsigned long)fsize);
          }
        } else {
          decode_text(buf, fsize, dest, dest_sz);
          found = true;
        }
      }
      free(buf);
    } else {
      fseek(f, (long)fsize, SEEK_CUR);
    }
    offset += fsize;
  }

  return found;
}

/* ------------------------------------------------------------------ public */

bool mp3_read_tags(const char *path, mp3_tags_t *out) {
  if (!path || !out)
    return false;

  memset(out, 0, sizeof(*out));

  FILE *f = id3_fopen(path, "rb");
  if (!f)
    return false;

  bool found = read_id3v2(f, out);
  if (!found)
    found = read_id3v1(f, out);

  fclose(f);
  return found;
}
