#include "ui_priv.h"

#include "ui_theme.h"

lv_obj_t *screen_lowbatt_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *icon = lv_label_create(scr);
    lv_obj_set_style_text_font(icon, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_DANGER, 0);
    lv_label_set_text_static(icon, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_add_flag(icon, UI_FLAG_NO_TEXT);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -UI_PAD * 4);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_label_set_text_static(title, "Battery empty");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text_static(hint, "Plug in USB-C");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, UI_PAD * 3);

    return scr;
}
