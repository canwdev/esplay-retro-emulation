#include "platform_log.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static bool s_inited;
static FILE *s_log_file;

static void platform_log_init_once(void) {
  if (s_inited)
    return;
  s_inited = true;

#ifdef _WIN32
  if (!GetConsoleWindow()) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
  }
  SetConsoleOutputCP(CP_UTF8);
#endif

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  s_log_file = fopen("launcher_sim.log", "a");
  if (s_log_file)
    setvbuf(s_log_file, NULL, _IONBF, 0);
}

void platform_log(int level, const char *tag, const char *fmt, ...) {
  platform_log_init_once();

  const char *lv =
      (level <= PLATFORM_LOG_ERROR) ? "E"
      : (level == PLATFORM_LOG_WARN) ? "W"
      : (level == PLATFORM_LOG_INFO) ? "I"
                                    : "D";

  char line[1024];
  size_t off = 0;
  if (tag && tag[0])
    off += (size_t)snprintf(line + off, sizeof(line) - off, "%s/%s: ", lv, tag);
  else
    off += (size_t)snprintf(line + off, sizeof(line) - off, "%s: ", lv);

  if (tag && tag[0])
    fprintf(stderr, "%s/%s: ", lv, tag);
  else
    fprintf(stderr, "%s: ", lv);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fputc('\n', stderr);

  va_start(ap, fmt);
  if (off < sizeof(line))
    vsnprintf(line + off, sizeof(line) - off, fmt, ap);
  va_end(ap);

  size_t n = strnlen(line, sizeof(line));
  if (n < sizeof(line) - 1) {
    line[n++] = '\n';
    line[n] = '\0';
  } else {
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = '\0';
  }

  if (s_log_file)
    fwrite(line, 1, n, s_log_file);

#ifdef _WIN32
  OutputDebugStringA(line);
#endif
}
