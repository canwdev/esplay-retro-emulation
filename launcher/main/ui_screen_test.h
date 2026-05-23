#ifndef UI_SCREEN_TEST_H
#define UI_SCREEN_TEST_H

#include <stdbool.h>

void ui_screen_test_open(void);
void ui_screen_test_close(void);
bool ui_screen_test_is_active(void);
void ui_screen_test_on_key(bool left, bool right);

#endif
