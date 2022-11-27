#ifndef __ESPLAY_UI__
#define __ESPLAY_UI__

#include <string.h>
#include <stdint.h>
#include "ugui.h"

void ui_clear_screen();
void ui_flush();
void ui_init();
void ui_deinit();
uint16_t *ui_get_fb();

#endif /*__ESPLAY_UI__*/