#include "ui_theme.h"

lv_style_t ui_style_well;
lv_style_t ui_style_tab;
lv_style_t ui_style_focus;
lv_style_t ui_style_chip;
lv_style_t ui_style_toast;
lv_style_t ui_style_caps;

void ui_theme_init(lv_display_t *disp)
{
    /* The default theme gives every screen and top-layer object the small font as their inherited text font. */
    lv_theme_t *theme = lv_theme_default_init(disp, UI_C_CREAM, UI_C_FOG, true, UI_FONT_SMALL);
    lv_display_set_theme(disp, theme);

    lv_style_init(&ui_style_well);
    lv_style_set_bg_color(&ui_style_well, UI_C_SURFACE);
    lv_style_set_bg_opa(&ui_style_well, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_well, UI_R_WELL);

    lv_style_init(&ui_style_tab);
    lv_style_set_bg_color(&ui_style_tab, UI_C_SURFACE2);
    lv_style_set_bg_opa(&ui_style_tab, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_tab, UI_R_TAB);

    lv_style_init(&ui_style_focus);
    lv_style_set_bg_color(&ui_style_focus, UI_C_CREAM);
    lv_style_set_bg_opa(&ui_style_focus, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_focus, UI_R_ROW);

    lv_style_init(&ui_style_chip);
    lv_style_set_bg_opa(&ui_style_chip, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_chip, UI_R_CHIP);

    lv_style_init(&ui_style_toast);
    lv_style_set_bg_color(&ui_style_toast, UI_C_SURFACE2);
    lv_style_set_bg_opa(&ui_style_toast, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_toast, UI_R_TOAST);
    lv_style_set_border_color(&ui_style_toast, UI_C_LINE);
    lv_style_set_border_width(&ui_style_toast, 1);

    lv_style_init(&ui_style_caps);
    lv_style_set_text_font(&ui_style_caps, UI_FONT_SMALL);
    lv_style_set_text_letter_space(&ui_style_caps, UI_CAPS_LETTER_SPACE);
    lv_style_set_text_color(&ui_style_caps, UI_C_FOG);
}
