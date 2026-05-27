#include "platform_log.h"

#include <stdarg.h>
#include <stdio.h>

void platform_log(int level, const char *tag, const char *fmt, ...) {
  (void)level;
  if (tag && tag[0])
    fprintf(stderr, "[%s] ", tag);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fputc('\n', stderr);
}
