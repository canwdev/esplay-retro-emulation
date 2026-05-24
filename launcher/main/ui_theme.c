#include "ui_font.h"
#include "ui_theme.h"

typedef struct {
  const char *name;
  uint32_t bg;
  uint32_t panel;
  uint32_t text;
  uint32_t text_dim;
  uint32_t accent;
  uint32_t focus_bg;
} ui_theme_palette_t;

static const ui_theme_palette_t s_palettes[UI_THEME_COUNT] = {
    {"Dark", 0x0A0A0A, 0x141414, 0xB0B0B0, 0x606060, 0x00E676, 0x123820},
    {"Light", 0xE8E8E8, 0xFFFFFF, 0x222222, 0x666666, 0x007A3D, 0x6FD098},
    {"Red Dark", 0x0C0808, 0x181010, 0xD8B8B8, 0x685050, 0xFF5252, 0x3A1414},
    {"Red Light", 0xF5EDED, 0xFFFFFF, 0x2A1515, 0x8A6060, 0xD32F2F, 0xF8BBBB},
    {"Yellow Dark", 0x0C0B06, 0x18160E, 0xD8D0B0, 0x686040, 0xFFD740, 0x3A300E},
    {"Yellow Light", 0xF5F2E8, 0xFFFFFF, 0x2A2410, 0x8A7040, 0xF9A825, 0xFFE082},
    {"Purple Dark", 0x0A080E, 0x16121C, 0xC8B8D8, 0x605068, 0xB388FF, 0x241638},
    {"Purple Light", 0xF0EBF8, 0xFFFFFF, 0x221633, 0x726080, 0x7C4DFF, 0xD1C4E9},
    {"Blue Dark", 0x060812, 0x101422, 0xB0B8D8, 0x505868, 0x448AFF, 0x102044},
    {"Blue Light", 0xE8EEF8, 0xFFFFFF, 0x101828, 0x506080, 0x1565C0, 0x90CAF9},
};

static lv_color_t s_bg;
static lv_color_t s_panel;
static lv_color_t s_text;
static lv_color_t s_text_dim;
static lv_color_t s_accent;
static lv_color_t s_focus_bg;

static lv_style_t s_style_list_btn;
static lv_style_t s_style_list_btn_focus;
static bool s_inited;
static int s_theme_id;

static void ui_theme_apply_palette(int theme_id) {
  if (theme_id < 0 || theme_id >= UI_THEME_COUNT)
    theme_id = UI_THEME_DARK;
  s_theme_id = theme_id;

  const ui_theme_palette_t *p = &s_palettes[s_theme_id];
  s_bg = lv_color_hex(p->bg);
  s_panel = lv_color_hex(p->panel);
  s_text = lv_color_hex(p->text);
  s_text_dim = lv_color_hex(p->text_dim);
  s_accent = lv_color_hex(p->accent);
  s_focus_bg = lv_color_hex(p->focus_bg);
}

static void ui_theme_style_focus_labels(lv_obj_t *obj) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      lv_obj_set_style_text_color(child, s_text, 0);
      lv_obj_set_style_text_color(child, s_accent, LV_STATE_FOCUSED);
      lv_obj_set_style_text_color(child, s_accent, LV_STATE_FOCUS_KEY);
#if LV_USE_IMAGE
    } else if (lv_obj_check_type(child, &lv_image_class)) {
      lv_obj_set_style_image_recolor(child, s_text, 0);
      lv_obj_set_style_image_recolor_opa(child, LV_OPA_COVER, 0);
      lv_obj_set_style_image_recolor(child, s_accent, LV_STATE_FOCUSED);
      lv_obj_set_style_image_recolor(child, s_accent, LV_STATE_FOCUS_KEY);
      lv_obj_set_style_image_recolor_opa(child, LV_OPA_COVER, LV_STATE_FOCUSED);
      lv_obj_set_style_image_recolor_opa(child, LV_OPA_COVER, LV_STATE_FOCUS_KEY);
#endif
    } else {
      ui_theme_style_focus_labels(child);
    }
  }
}

static void ui_theme_init_styles(void) {
  lv_style_init(&s_style_list_btn);
  lv_style_set_radius(&s_style_list_btn, 0);
  lv_style_set_bg_opa(&s_style_list_btn, LV_OPA_COVER);
  lv_style_set_bg_color(&s_style_list_btn, s_panel);
  lv_style_set_border_width(&s_style_list_btn, 0);
  lv_style_set_shadow_width(&s_style_list_btn, 0);
  lv_style_set_outline_width(&s_style_list_btn, 0);
  lv_style_set_outline_opa(&s_style_list_btn, LV_OPA_TRANSP);
  lv_style_set_text_color(&s_style_list_btn, s_text);
  lv_style_set_text_font(&s_style_list_btn, ui_font_default());
  lv_style_set_pad_all(&s_style_list_btn, 4);

  lv_style_init(&s_style_list_btn_focus);
  lv_style_set_bg_color(&s_style_list_btn_focus, s_focus_bg);
  lv_style_set_text_color(&s_style_list_btn_focus, s_accent);
  lv_style_set_outline_width(&s_style_list_btn_focus, 0);
  lv_style_set_outline_opa(&s_style_list_btn_focus, LV_OPA_TRANSP);
}

void ui_theme_init(void) {
  if (!s_inited)
    ui_theme_apply_palette(UI_THEME_DARK);
  ui_theme_init_styles();
  s_inited = true;
}

void ui_theme_set(int theme_id) {
  ui_theme_apply_palette(theme_id);
  ui_theme_init_styles();
  s_inited = true;
}

int ui_theme_get(void) { return s_theme_id; }

const char *ui_theme_name(int theme_id) {
  if (theme_id < 0 || theme_id >= UI_THEME_COUNT)
    theme_id = UI_THEME_DARK;
  return s_palettes[theme_id].name;
}

bool ui_theme_is_light(void) { return (s_theme_id & 1) != 0; }

void ui_theme_apply_screen(lv_obj_t *screen) {
  lv_obj_set_style_bg_color(screen, s_bg, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_style_text_font(screen, ui_font_default(), 0);
}

void ui_theme_style_label_primary(lv_obj_t *label) {
  lv_obj_set_style_text_color(label, s_text, 0);
}

void ui_theme_style_label_secondary(lv_obj_t *label) {
  lv_obj_set_style_text_color(label, s_text_dim, 0);
}

void ui_theme_style_label_accent(lv_obj_t *label) {
  lv_obj_set_style_text_color(label, s_accent, 0);
}

void ui_theme_style_btn(lv_obj_t *btn) { ui_theme_style_list_btn(btn); }

void ui_theme_style_list(lv_obj_t *list) {
  lv_obj_set_style_bg_color(list, s_panel, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_border_color(list, s_text_dim, 0);
  lv_obj_set_style_radius(list, 2, 0);
  lv_obj_set_style_pad_row(list, 2, 0);
  lv_obj_set_style_text_color(list, s_text, 0);
}

void ui_theme_style_list_btn(lv_obj_t *btn) {
  lv_obj_add_style(btn, &s_style_list_btn, 0);
  lv_obj_add_style(btn, &s_style_list_btn, LV_STATE_FOCUS_KEY);
  lv_obj_add_style(btn, &s_style_list_btn_focus, LV_STATE_FOCUSED);
  lv_obj_add_style(btn, &s_style_list_btn_focus, LV_STATE_FOCUS_KEY);
  lv_obj_add_style(btn, &s_style_list_btn_focus, LV_STATE_PRESSED);
  ui_theme_style_focus_labels(btn);
}

static void ui_theme_style_msgbox_header_btn(lv_obj_t *btn) {
  ui_theme_style_list_btn(btn);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(btn); i++) {
    lv_obj_t *child = lv_obj_get_child(btn, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      ui_theme_style_label_accent(child);
    }
#if LV_USE_IMAGE
    else if (lv_obj_check_type(child, &lv_image_class)) {
      lv_obj_set_style_image_recolor(child, s_accent, 0);
      lv_obj_set_style_image_recolor_opa(child, LV_OPA_COVER, 0);
    }
#endif
  }
}

void ui_theme_style_msgbox(lv_obj_t *mbox) {
  lv_obj_set_style_bg_color(mbox, s_panel, 0);
  lv_obj_set_style_bg_opa(mbox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(mbox, s_accent, 0);
  lv_obj_set_style_border_width(mbox, 1, 0);
  lv_obj_set_style_text_color(mbox, s_text, 0);
  lv_obj_set_style_radius(mbox, 2, 0);
  lv_obj_set_style_shadow_width(mbox, 0, 0);
}

static void ui_theme_style_tree(lv_obj_t *obj) {
  if (lv_obj_check_type(obj, &lv_list_class)) {
    ui_theme_style_list(obj);
  } else if (lv_obj_check_type(obj, &lv_button_class)) {
    ui_theme_style_list_btn(obj);
  } else if (lv_obj_check_type(obj, &lv_label_class)) {
    ui_theme_style_label_primary(obj);
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++)
    ui_theme_style_tree(lv_obj_get_child(obj, i));
}

void ui_theme_apply_msgbox(lv_obj_t *mbox) {
  ui_theme_style_msgbox(mbox);
  lv_obj_set_width(mbox, 280);

  lv_obj_t *backdrop = lv_obj_get_parent(mbox);
  if (backdrop && lv_obj_check_type(backdrop, &lv_msgbox_backdrop_class)) {
    lv_obj_set_style_bg_color(backdrop, s_bg, 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
  }

  lv_obj_t *header = lv_msgbox_get_header(mbox);
  if (header) {
    lv_obj_set_style_bg_color(header, s_panel, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 8, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(header); i++) {
      lv_obj_t *child = lv_obj_get_child(header, i);
      if (lv_obj_check_type(child, &lv_msgbox_header_button_class))
        ui_theme_style_msgbox_header_btn(child);
    }
  }

  lv_obj_t *title = lv_msgbox_get_title(mbox);

  lv_obj_t *content = lv_msgbox_get_content(mbox);
  if (content) {
    lv_obj_set_style_bg_color(content, s_panel, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(content, 8, 0);
  }

  lv_obj_t *footer = lv_msgbox_get_footer(mbox);
  if (footer) {
    lv_obj_set_style_bg_color(footer, s_panel, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
  }

  ui_theme_style_tree(mbox);

  if (title)
    ui_theme_style_label_accent(title);
}

void ui_theme_style_bar(lv_obj_t *bar) {
  lv_obj_set_style_bg_color(bar, s_text_dim, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, s_accent, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
}

void ui_theme_style_panel(lv_obj_t *obj) {
  lv_obj_set_style_bg_color(obj, s_panel, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, s_text_dim, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, 2, 0);
}

void ui_theme_style_scroll(lv_obj_t *obj) {
  lv_obj_set_style_bg_color(obj, s_panel, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, s_text_dim, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, 2, 0);
  lv_obj_set_style_width(obj, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(obj, s_text_dim, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(obj, LV_OPA_40, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(obj, 2, LV_PART_SCROLLBAR);
}

lv_color_t ui_theme_color_bg(void) { return s_bg; }
lv_color_t ui_theme_color_accent(void) { return s_accent; }
lv_color_t ui_theme_color_text(void) { return s_text; }
lv_color_t ui_theme_color_text_dim(void) { return s_text_dim; }
lv_color_t ui_theme_color_panel(void) { return s_panel; }
