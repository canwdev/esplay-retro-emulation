#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
  TEXT_ENC_UTF8 = 0,
  TEXT_ENC_GBK,
} text_encoding_t;

/** Detect encoding; decode to UTF-8 heap buffer (caller must free). Returns NULL on failure. */
char *text_decode_to_utf8(const uint8_t *raw, size_t raw_len, size_t *out_len,
                            text_encoding_t *out_enc);
