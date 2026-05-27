#pragma once

enum {
  PLATFORM_LOG_ERROR = 0,
  PLATFORM_LOG_WARN,
  PLATFORM_LOG_INFO,
  PLATFORM_LOG_DEBUG,
};

void platform_log(int level, const char *tag, const char *fmt, ...);
