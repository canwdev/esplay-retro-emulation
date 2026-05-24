#ifndef INPUT_BRIDGE_H
#define INPUT_BRIDGE_H

#include "lvgl.h"

void input_bridge_init(void);
void input_bridge_lvgl_read(lv_indev_t *indev, lv_indev_data_t *data);
void input_bridge_poll(void);
/* Suppress LVGL key events until A is released (avoids re-entering Files
 * after leaving via the Back row while A is still held). */
void input_bridge_block_enter_until_release(void);

#endif
