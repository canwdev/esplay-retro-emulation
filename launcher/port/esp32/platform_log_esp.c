#include "platform_log.h"

#include "esp_log.h"

#include <stdarg.h>
#include <stdio.h>

void platform_log(int level, const char *tag, const char *fmt, ...) {
  esp_log_level_t esp_level = ESP_LOG_INFO;
  if (level <= PLATFORM_LOG_ERROR)
    esp_level = ESP_LOG_ERROR;
  else if (level == PLATFORM_LOG_WARN)
    esp_level = ESP_LOG_WARN;
  else if (level == PLATFORM_LOG_INFO)
    esp_level = ESP_LOG_INFO;
  else
    esp_level = ESP_LOG_DEBUG;

  char buf[384];
  buf[0] = '\0';

  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);
  va_end(ap);

  size_t len = 0;
  while (len < sizeof(buf) && buf[len] != '\0')
    len++;

  if (len == 0) {
    buf[0] = '\n';
    buf[1] = '\0';
  } else if (buf[len - 1] != '\n') {
    if (len + 1 < sizeof(buf)) {
      buf[len] = '\n';
      buf[len + 1] = '\0';
    } else {
      buf[sizeof(buf) - 2] = '\n';
      buf[sizeof(buf) - 1] = '\0';
    }
  }

  (void)n;
  esp_log_write(esp_level, tag ? tag : "", "%s", buf);
}
