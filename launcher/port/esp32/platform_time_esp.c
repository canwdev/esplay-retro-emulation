#include "platform_time.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t platform_millis(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void platform_sleep_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}
