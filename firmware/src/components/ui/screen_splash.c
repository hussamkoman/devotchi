#include "ui_priv.h"

#include "ui_lockup.h"
#include "ui_theme.h"

#include "esp_app_desc.h"

#include <stdio.h>

/* The lock-up block sits in the middle of the screen: the start-up pet has no rig instance yet to stand above it. */
#define LOCKUP_Y   84
#define TAGLINE_Y  (LOCKUP_Y + UI_LOCKUP_H + 8)
#define VERSION_Y  216

lv_obj_t *screen_splash_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    const int32_t x = (LV_HOR_RES - UI_LOCKUP_W) / 2;
    lv_obj_t *word = lv_image_create(scr);
    lv_image_set_src(word, &ui_img_lockup);
    lv_obj_set_style_image_recolor(word, UI_C_CREAM, 0);
    lv_obj_set_style_image_recolor_opa(word, LV_OPA_COVER, 0);
    lv_obj_set_pos(word, x, LOCKUP_Y);
    lv_obj_t *tip = lv_image_create(scr);
    lv_image_set_src(tip, &ui_img_lockup_tip);
    lv_obj_set_style_image_recolor(tip, UI_C_MANGO, 0);
    lv_obj_set_style_image_recolor_opa(tip, LV_OPA_COVER, 0);
    lv_obj_set_pos(tip, x + UI_LOCKUP_TIP_X, LOCKUP_Y + UI_LOCKUP_TIP_Y);

    lv_obj_t *tagline = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, TAGLINE_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(tagline, "your agent's desk pet");

    char version[40];
    snprintf(version, sizeof(version), "v%s", esp_app_get_description()->version);
    lv_obj_t *v = ui_text_line(scr, UI_FONT_MONO, UI_C_SLATE, VERSION_Y, LV_TEXT_ALIGN_CENTER);
    ui_label_set(v, version);
    return scr;
}
