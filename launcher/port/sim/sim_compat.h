#pragma once

#include <stddef.h>
#include <string.h>

#ifdef _MSC_VER
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m)&_S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m)&_S_IFMT) == _S_IFREG)
#endif
#endif

static inline size_t strlcpy(char *dst, const char *src, size_t dstsz) {
  size_t srclen = src ? strlen(src) : 0;
  if (dstsz) {
    size_t n = (srclen >= dstsz) ? (dstsz - 1) : srclen;
    if (n)
      memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}

static inline size_t strlcat(char *dst, const char *src, size_t dstsz) {
  size_t dlen = 0;
  while (dlen < dstsz && dst[dlen] != '\0')
    dlen++;

  size_t srclen = src ? strlen(src) : 0;
  if (dlen == dstsz)
    return dlen + srclen;

  size_t space = dstsz - dlen - 1;
  size_t n = (srclen > space) ? space : srclen;
  if (n)
    memcpy(dst + dlen, src, n);
  dst[dlen + n] = '\0';
  return dlen + srclen;
}
