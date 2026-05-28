#include "input_bridge.h"
#include "file_manager.h"
#include "hal_input.h"
#include "preview.h"
#include "ui_app.h"
#include "ui_home.h"
#include "ui_screen_test.h"
#include "ui_settings.h"
#include "ui_backlight.h"

static input_gamepad_state s_last;
static bool s_last_valid;
static lv_timer_t *s_poll_timer;
static bool s_block_enter_until_a_release;
static bool s_swallow_until_all_released;
static bool s_poll_dimmed;

#define INPUT_POLL_MS_ACTIVE 20
#define INPUT_POLL_MS_DIMMED 50

void input_bridge_block_enter_until_release(void) {
  s_block_enter_until_a_release = true;
}

static bool input_edge(const input_gamepad_state *cur, int idx) {
  return cur->values[idx] == 1 &&
         (!s_last_valid || s_last.values[idx] == 0);
}

static bool input_any_pressed(const input_gamepad_state *gp) {
  for (int i = 0; i < GAMEPAD_INPUT_MAX; i++) {
    if (gp->values[i] == 1)
      return true;
  }
  return false;
}

static void input_bridge_sync_poll_period(void) {
  bool dimmed = !ui_backlight_is_on();
  if (dimmed == s_poll_dimmed || !s_poll_timer)
    return;
  s_poll_dimmed = dimmed;
  lv_timer_set_period(s_poll_timer, dimmed ? INPUT_POLL_MS_DIMMED
                                           : INPUT_POLL_MS_ACTIVE);
}

static void input_poll_timer_cb(lv_timer_t *t) {
  (void)t;
  input_bridge_sync_poll_period();

  input_gamepad_state gp;
  hal_input_poll();
  hal_input_read(&gp);

  bool any_pressed = input_any_pressed(&gp);

  /* Handle global backlight logic. */
  bool any_edge = false;
  for (int i = 0; i < GAMEPAD_INPUT_MAX; i++) {
    if (input_edge(&gp, i)) {
      any_edge = true;
      break;
    }
  }

  if (any_edge && !ui_backlight_is_on()) {
    ui_backlight_set_on(true);
    s_swallow_until_all_released = true;
    /* Swallow the wake-up key press. */
    s_last = gp;
    s_last_valid = true;
    return;
  }

  if (any_pressed) {
    ui_backlight_refresh_timeout();
  }
  ui_backlight_process();

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

  if (fm_uses_direct_nav()) {
    if (edge[GAMEPAD_INPUT_UP])
      fm_on_nav_key(LV_KEY_UP);
    else if (edge[GAMEPAD_INPUT_DOWN])
      fm_on_nav_key(LV_KEY_DOWN);
    else if (edge[GAMEPAD_INPUT_LEFT])
      fm_on_nav_key(LV_KEY_LEFT);
    else if (edge[GAMEPAD_INPUT_RIGHT])
      fm_on_nav_key(LV_KEY_RIGHT);
    else
      fm_on_nav_hold_tick(gp.values[GAMEPAD_INPUT_UP] == 1,
                          gp.values[GAMEPAD_INPUT_DOWN] == 1,
                          gp.values[GAMEPAD_INPUT_LEFT] == 1,
                          gp.values[GAMEPAD_INPUT_RIGHT] == 1);
    if (edge[GAMEPAD_INPUT_A])
      fm_on_nav_key(LV_KEY_ENTER);
  }

  s_last = gp;
  s_last_valid = true;
}

void input_bridge_init(void) {
  s_last_valid = false;
  s_swallow_until_all_released = false;
  s_poll_dimmed = false;
  s_poll_timer = lv_timer_create(input_poll_timer_cb, INPUT_POLL_MS_ACTIVE, NULL);
  lv_timer_set_repeat_count(s_poll_timer, -1);
  input_bridge_sync_poll_period();
}

void input_bridge_lvgl_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (!ui_backlight_is_on()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  input_gamepad_state gamepad_state;
  hal_input_poll();
  hal_input_read(&gamepad_state);

  data->state = LV_INDEV_STATE_RELEASED;

  if (s_swallow_until_all_released) {
    if (!input_any_pressed(&gamepad_state))
      s_swallow_until_all_released = false;
    return;
  }

  if (s_block_enter_until_a_release) {
    if (gamepad_state.values[GAMEPAD_INPUT_A] == 0)
      s_block_enter_until_a_release = false;
    else
      return;
  }

  if (preview_is_active())
    return;

  if (ui_screen_test_is_active())
    return;

  if (fm_uses_direct_nav())
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
