#include "ui_priv.h"

#include "ui_theme.h"

/* The battery glyph of the mockup (92): a thick outline, its nub and one low cell. */
#define BATT_X       103
#define BATT_Y       49
#define BATT_W       100
#define BATT_H       54
#define BATT_R       14
#define BATT_BORDER  5
#define NUB_W        9
#define NUB_H        20
#define CELL_W       12
#define CELL_H       34
#define TITLE_Y      124
#define HINT_Y       156

lv_obj_t *screen_lowbatt_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *outline = ui_box(scr, BATT_X, BATT_Y, BATT_W, BATT_H);
    lv_obj_set_style_radius(outline, BATT_R, 0);
    lv_obj_set_style_border_width(outline, BATT_BORDER, 0);
    lv_obj_set_style_border_color(outline, UI_C_CORAL, 0);
    ui_rect(scr, BATT_X + BATT_W + 5, BATT_Y + (BATT_H - NUB_H) / 2, NUB_W, NUB_H, UI_C_CORAL, 4);
    ui_rect(scr, BATT_X + 10, BATT_Y + (BATT_H - CELL_H) / 2, CELL_W, CELL_H, UI_C_CORAL, 5);

    lv_obj_t *title = ui_text_line(scr, UI_FONT_TITLE, UI_C_CREAM, TITLE_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(title, "Battery empty");
    lv_obj_t *hint = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, HINT_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(hint, "Plug in USB-C to wake me up");
    return scr;
}
