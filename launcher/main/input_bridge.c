#include "input_bridge.h"
#include "file_manager.h"
#include "gamepad.h"
#include "preview.h"
#include "ui_app.h"
#include "ui_home.h"
#include "ui_screen_test.h"
#include "ui_settings.h"

static input_gamepad_state s_last;
static bool s_last_valid;
static lv_timer_t *s_poll_timer;

static bool input_edge(const input_gamepad_state *cur, int idx) {
  return cur->values[idx] == 1 &&
         (!s_last_valid || s_last.values[idx] == 0);
}

static void input_poll_timer_cb(lv_timer_t *t) {
  (void)t;
  input_gamepad_state gp;
  gamepad_read(&gp);

  bool edge[GAMEPAD_INPUT_MAX];
  for (int i = 0; i < GAMEPAD_INPUT_MAX; i++)
    edge[i] = input_edge(&gp, i);

  if (preview_is_active()) {
    preview_on_key(&gp, edge);
    if (edge[GAMEPAD_INPUT_B])
      fm_handle_back();
    preview_on_timer();
    s_last = gp;
    s_last_valid = true;
    return;
  }

  if (edge[GAMEPAD_INPUT_B]) {
    if (g_ui.current_page == PAGE_FILES)
      fm_handle_back();
    else if (g_ui.current_page == PAGE_SETTINGS)
      ui_settings_handle_back();
    else if (g_ui.current_page == PAGE_SCREEN_TEST)
      ui_screen_test_close();
  }

  if (g_ui.current_page == PAGE_SCREEN_TEST) {
    ui_screen_test_on_key(edge[GAMEPAD_INPUT_LEFT], edge[GAMEPAD_INPUT_RIGHT]);
  }

  if (g_ui.current_page == PAGE_FILES && edge[GAMEPAD_INPUT_MENU])
    fm_handle_menu_on_focus();

  s_last = gp;
  s_last_valid = true;
}

void input_bridge_init(void) {
  s_last_valid = false;
  s_poll_timer = lv_timer_create(input_poll_timer_cb, 20, NULL);
  lv_timer_set_repeat_count(s_poll_timer, -1);
}

void input_bridge_lvgl_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  input_gamepad_state gamepad_state;
  gamepad_read(&gamepad_state);

  data->state = LV_INDEV_STATE_RELEASED;

  if (preview_is_active())
    return;

  if (ui_screen_test_is_active())
    return;

  if (gamepad_state.values[GAMEPAD_INPUT_UP] == 1) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_UP;
  } else if (gamepad_state.values[GAMEPAD_INPUT_DOWN] == 1) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_DOWN;
  } else if (gamepad_state.values[GAMEPAD_INPUT_LEFT] == 1) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_LEFT;
  } else if (gamepad_state.values[GAMEPAD_INPUT_RIGHT] == 1) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_RIGHT;
  } else if (gamepad_state.values[GAMEPAD_INPUT_A] == 1) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_ENTER;
  }
}

void input_bridge_poll(void) {
  if (s_poll_timer)
    input_poll_timer_cb(s_poll_timer);
}
