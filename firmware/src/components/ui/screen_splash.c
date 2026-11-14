#include "ui_priv.h"

#include "ui_theme.h"

#include "esp_app_desc.h"

lv_obj_t *screen_splash_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *name = lv_label_create(scr);
    lv_obj_set_style_text_font(name, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT, 0);
    lv_label_set_text_static(name, "Devotchi");
    lv_obj_align(name, LV_ALIGN_CENTER, 0, -UI_PAD * 2);

    lv_obj_t *version = lv_label_create(scr);
    lv_obj_set_style_text_font(version, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(version, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_align(version, LV_ALIGN_CENTER, 0, UI_PAD * 2);

    return scr;
}
