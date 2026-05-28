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
  /* ------------------- 绿色系 (Green / Emerald) ------------------- */
  // M3 Green Dark: 极深墨绿底，高亮采用春绿
  {"Green Dark",    0x0C0F0D, 0x181E1A, 0xE2E3E0, 0x8C928E, 0x00E676, 0x00522B},
  // M3 Green Light: 清爽薄荷乳白底
  {"Green Light",   0xF6FBF7, 0xFFFFFF, 0x191C1A, 0x5C635E, 0x007A3D, 0xC1E8CD},

  /* ------------------- 红色系 (Red / Error) ------------------- */
  // M3 Red Dark: 带有微弱红晕的黑底，高亮用 Material 鲜红
  {"Red Dark",      0x0F0C0C, 0x1F1919, 0xF2E4E4, 0x958888, 0xFF5252, 0x680003},
  // M3 Red Light: 温暖樱花白底
  {"Red Light",     0xFFFBFA, 0xFFFFFF, 0x221717, 0x655656, 0xBA1A1A, 0xFFDAD6},

  /* ------------------- 黄色系 (Yellow / Amber) ------------------- */
  // M3 Yellow Dark: 琥珀金暗底
  {"Yellow Dark",   0x0F0E0B, 0x1E1B12, 0xECE1D5, 0x968F85, 0xFFD740, 0x574300},
  // M3 Yellow Light: 象牙沙滩白
  {"Yellow Light",  0xFFFBFF, 0xFFFFFF, 0x1E1B16, 0x635E54, 0x7A5B00, 0xFFDF9E},

  /* ------------------- 紫色系 (Purple / Orchid) ------------------- */
  // M3 Purple Dark: 优雅薰衣草暗底
  {"Purple Dark",   0x0E0B12, 0x1C1625, 0xE7E0EC, 0x928F99, 0xB388FF, 0x4F378B},
  // M3 Purple Light: 丁香紫白底
  {"Purple Light",  0xFFFBFF, 0xFFFFFF, 0x1D192B, 0x625B71, 0x6750A4, 0xEADDFF},

  /* ------------------- 蓝色系 (Blue / Indigo) ------------------- */
  // M3 Blue Dark: 深邃星空蓝底
  {"Blue Dark",     0x0B0E14, 0x141B28, 0xE2E2E6, 0x8E9199, 0x448AFF, 0x00458F},
  // M3 Blue Light: 蔚蓝晴空底
  {"Blue Light",    0xF8F9FF, 0xFFFFFF, 0x191C20, 0x575E6A, 0x005FAF, 0xD4E3FF},

  /* =================== 以下为新增的 Material 颜色 =================== */

  /* ------------------- 橙色系 (Orange / Tangerine) ------------------- */
  // 工业/警示/运动感强烈
  {"Orange Dark",   0x100D0B, 0x201A16, 0xECE0DB, 0x978E8A, 0xFF9100, 0x623100},
  {"Orange Light",  0xFFFBFF, 0xFFFFFF, 0x201A17, 0x645C58, 0x8B5000, 0xFFDCBE},

  /* ------------------- 青色系 (Cyan / Teal) ------------------- */
  // 极具未来感与科技感，对 LCD 屏幕非常友好
  {"Cyan Dark",     0x0B0F10, 0x151E20, 0xE0E3E3, 0x899294, 0x00E5FF, 0x004F58},
  {"Cyan Light",    0xF4FAFA, 0xFFFFFF, 0x161D1E, 0x566162, 0x006A75, 0xA1EFFC},

  /* ------------------- 粉红系 (Pink / Rose) ------------------- */
  // 现代、时尚，高对比度
  {"Pink Dark",     0x100B0D, 0x211419, 0xECE0E2, 0x988E90, 0xFF4081, 0x630631},
  {"Pink Light",    0xFFFBFF, 0xFFFFFF, 0x201A1B, 0x645C5D, 0x8F496B, 0xFFD9E6},
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

void ui_theme_style_label_truncated(lv_obj_t *label, lv_coord_t width) {
  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (!font)
    font = ui_font_default();
  lv_coord_t lh = lv_font_get_line_height(font);
  if (width > 0)
    lv_obj_set_width(label, width);
  lv_obj_set_height(label, lh);
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_obj_set_style_pad_all(label, 0, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

void ui_theme_style_label_row(lv_obj_t *label, lv_coord_t row_h) {
  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (!font)
    font = ui_font_default();
  lv_coord_t lh = lv_font_get_line_height(font);
  lv_coord_t pad = (row_h - lh) / 2;
  if (pad < 0)
    pad = 0;
  lv_obj_set_height(label, row_h);
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_obj_set_style_pad_top(label, pad, 0);
  lv_obj_set_style_pad_bottom(label, 0, 0);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
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

  if (title) {
    ui_theme_style_label_accent(title);
    ui_theme_style_label_truncated(title, 264);
  }
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
lv_color_t ui_theme_color_focus_bg(void) { return s_focus_bg; }
