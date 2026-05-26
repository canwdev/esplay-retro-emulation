#ifndef UI_BACKLIGHT_H
#define UI_BACKLIGHT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize global backlight management.
 */
void ui_backlight_init(void);

/**
 * Periodic processing for auto-timeout.
 */
void ui_backlight_process(void);

/**
 * Reset the inactivity timer.
 */
void ui_backlight_refresh_timeout(void);

/**
 * Explicitly set backlight state.
 */
void ui_backlight_set_on(bool on);

/**
 * Check current backlight state.
 */
bool ui_backlight_is_on(void);

/**
 * Toggle backlight state manually.
 */
void ui_backlight_toggle(void);

/**
 * Set backlight timeout in seconds.
 */
void ui_backlight_set_timeout(int32_t seconds);

#endif
