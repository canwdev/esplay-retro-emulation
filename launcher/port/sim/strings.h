#pragma once

#include <string.h>

#ifdef _MSC_VER
#include <ctype.h>

static inline int strcasecmp(const char *a, const char *b) {
  return _stricmp(a, b);
}

static inline int strncasecmp(const char *a, const char *b, size_t n) {
  return _strnicmp(a, b, n);
}
#endif
