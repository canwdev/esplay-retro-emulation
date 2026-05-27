#include "lvgl.h"

#if LV_USE_SDL
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#endif

typedef struct {
    lv_obj_t *value_label;
    lv_obj_t *status_label;
    lv_obj_t *bar;
    lv_obj_t *arc;
    lv_obj_t *tap_count_label;
    lv_obj_t *spinner;
    int32_t tap_count;
} demo_ui_t;

static demo_ui_t s_ui;

static void demo_set_bar_color(lv_obj_t *bar, uint32_t palette)
{
    lv_obj_set_style_bg_color(bar, lv_palette_main(palette), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_palette_darken(palette, 1), LV_PART_MAIN);
}

static void demo_update_value(int32_t v)
{
    lv_bar_set_value(s_ui.bar, v, LV_ANIM_ON);
    lv_arc_set_value(s_ui.arc, v);
    lv_label_set_text_fmt(s_ui.value_label, "Value: %ld%%", (long)v);
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    demo_update_value(lv_slider_get_value(slider));
}

static void switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    lv_label_set_text(s_ui.status_label, on ? LV_SYMBOL_OK " On" : LV_SYMBOL_CLOSE " Off");
    lv_obj_set_style_text_color(s_ui.status_label,
                                on ? lv_palette_main(LV_PALETTE_GREEN)
                                   : lv_palette_main(LV_PALETTE_GREY),
                                0);
}

static void checkbox_event_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    if (checked)
        lv_obj_clear_flag(s_ui.spinner, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_ui.spinner, LV_OBJ_FLAG_HIDDEN);
}

static void dropdown_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint32_t sel = lv_dropdown_get_selected(dd);
    static const uint32_t palettes[] = {
        LV_PALETTE_BLUE, LV_PALETTE_GREEN, LV_PALETTE_ORANGE,
        LV_PALETTE_PURPLE, LV_PALETTE_RED,
    };
    demo_set_bar_color(s_ui.bar, palettes[sel % 5]);
}

static void anim_nudge_x(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    s_ui.tap_count++;
    lv_label_set_text_fmt(s_ui.tap_count_label, "Taps: %d", s_ui.tap_count);

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, LV_SYMBOL_REFRESH " Again!");

    int32_t x0 = lv_obj_get_x(btn);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, btn);
    lv_anim_set_exec_cb(&anim, anim_nudge_x);
    lv_anim_set_values(&anim, x0 - 8, x0 + 8);
    lv_anim_set_duration(&anim, 90);
    lv_anim_set_playback_duration(&anim, 90);
    lv_anim_set_repeat_count(&anim, 2);
    lv_anim_start(&anim);
}

static void demo_build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_palette_lighten(LV_PALETTE_GREY, 4), 0);
    lv_obj_set_style_pad_all(scr, 10, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 8, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_HOME " LVGL Feature Demo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);

    s_ui.value_label = lv_label_create(scr);
    lv_label_set_text(s_ui.value_label, "Value: 50%");

    /* Slider + arc row */
    lv_obj_t *gauge_row = lv_obj_create(scr);
    lv_obj_remove_flag(gauge_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(gauge_row, lv_pct(100));
    lv_obj_set_height(gauge_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gauge_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_row, 0, 0);
    lv_obj_set_style_pad_all(gauge_row, 0, 0);
    lv_obj_set_flex_flow(gauge_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gauge_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *slider = lv_slider_create(gauge_row);
    lv_obj_set_width(slider, 260);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_ui.arc = lv_arc_create(gauge_row);
    lv_obj_set_size(s_ui.arc, 72, 72);
    lv_arc_set_range(s_ui.arc, 0, 100);
    lv_arc_set_value(s_ui.arc, 50);
    lv_obj_remove_style(s_ui.arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_ui.arc, LV_OBJ_FLAG_CLICKABLE);

    /* Progress bar */
    s_ui.bar = lv_bar_create(scr);
    lv_obj_set_width(s_ui.bar, lv_pct(100));
    lv_bar_set_range(s_ui.bar, 0, 100);
    lv_bar_set_value(s_ui.bar, 50, LV_ANIM_OFF);
    demo_set_bar_color(s_ui.bar, LV_PALETTE_BLUE);

    /* Switch row */
    lv_obj_t *switch_row = lv_obj_create(scr);
    lv_obj_remove_flag(switch_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(switch_row, lv_pct(100));
    lv_obj_set_height(switch_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(switch_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(switch_row, 0, 0);
    lv_obj_set_style_pad_all(switch_row, 0, 0);
    lv_obj_set_flex_flow(switch_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(switch_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(switch_row, 12, 0);

    lv_obj_t *sw_label = lv_label_create(switch_row);
    lv_label_set_text(sw_label, "Notify");

    lv_obj_t *sw = lv_switch_create(switch_row);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_ui.status_label = lv_label_create(switch_row);
    lv_label_set_text(s_ui.status_label, LV_SYMBOL_OK " On");
    lv_obj_set_style_text_color(s_ui.status_label, lv_palette_main(LV_PALETTE_GREEN), 0);

    /* Controls row: button, checkbox, spinner, dropdown */
    lv_obj_t *ctrl_row = lv_obj_create(scr);
    lv_obj_remove_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(ctrl_row, lv_pct(100));
    lv_obj_set_height(ctrl_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, 10, 0);
    lv_obj_set_style_pad_row(ctrl_row, 6, 0);

    lv_obj_t *btn = lv_button_create(ctrl_row);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_PLAY " Tap me");

    s_ui.tap_count_label = lv_label_create(ctrl_row);
    lv_label_set_text(s_ui.tap_count_label, "Taps: 0");

    lv_obj_t *cb = lv_checkbox_create(ctrl_row);
    lv_checkbox_set_text(cb, "Spinner");
    lv_obj_add_event_cb(cb, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_ui.spinner = lv_spinner_create(ctrl_row);
    lv_obj_set_size(s_ui.spinner, 28, 28);
    lv_spinner_set_anim_params(s_ui.spinner, 800, 200);
    lv_obj_add_flag(s_ui.spinner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *dd = lv_dropdown_create(scr);
    lv_dropdown_set_options(dd, "Blue\nGreen\nOrange\nPurple\nRed");
    lv_obj_set_width(dd, 140);
    lv_obj_add_event_cb(dd, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Footer hint */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint,
                      LV_SYMBOL_BARS " Slider  " LV_SYMBOL_REFRESH " Anim  "
                      LV_SYMBOL_TINT " Theme  " LV_SYMBOL_KEYBOARD " Keyboard");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
}

int main(void)
{
    lv_init();

#if LV_USE_SDL
    lv_display_t *disp = lv_sdl_window_create(480, 320);
    (void)disp;
    lv_indev_t *mouse = lv_sdl_mouse_create();
    (void)mouse;
    lv_indev_t *kb = lv_sdl_keyboard_create();
    (void)kb;
#else
#error "This demo requires LV_USE_SDL"
#endif

    demo_build_ui();

    while (1) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    return 0;
}
