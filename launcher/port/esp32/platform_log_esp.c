#include "platform_log.h"

#include "esp_log.h"

#include <stdarg.h>

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

  va_list ap;
  va_start(ap, fmt);
  esp_log_writev(esp_level, tag ? tag : "", fmt, ap);
  va_end(ap);
}
