#ifndef INPUT_BRIDGE_H
#define INPUT_BRIDGE_H

#include "lvgl.h"

void input_bridge_init(void);
void input_bridge_lvgl_read(lv_indev_t *indev, lv_indev_data_t *data);
void input_bridge_poll(void);

#endif
