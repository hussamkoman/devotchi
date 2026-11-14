#include "ui_priv.h"

#include "pair_code.h"
#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>

#define BAR_HEIGHT  6
#define BAR_Y       140

static lv_obj_t *s_code;
static lv_obj_t *s_how;
static lv_obj_t *s_bar;
static lv_obj_t *s_host;
static lv_obj_t *s_error;

lv_obj_t *screen_pairing_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *title = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_ACCENT, UI_PAD, true);
    lv_label_set_text_static(title, "Pair device");

    s_code = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_TEXT, 58, true);
    lv_obj_set_style_text_align(s_code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(s_code, 4, 0);
    s_how = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT, 108, true);
    lv_obj_set_style_text_align(s_how, LV_TEXT_ALIGN_CENTER, 0);

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, LV_HOR_RES - 2 * UI_PAD, BAR_HEIGHT);
    lv_obj_align(s_bar, LV_ALIGN_TOP_LEFT, UI_PAD, BAR_Y);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 1);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    s_host = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 158, true);
    s_error = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_WARN, 186, false);
    return scr;
}

void ui_pairing_set(const char *code, const char *host, const char *error, uint32_t remaining_s, uint32_t total_s)
{
    char display[PAIR_CODE_DISPLAY_SIZE];
    char how[48] = "";
    if (pair_code_format(code, display)) {
        snprintf(how, sizeof(how), "On your PC: devotchi pair %s", code);
    } else {
        snprintf(display, sizeof(display), "--- ---");
        remaining_s = 0;
    }
    if (total_s == 0) {
        total_s = 1;
    }
    if (remaining_s > total_s) {
        remaining_s = total_s;
    }

    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_code, display);
    ui_label_set(s_how, how);
    ui_label_set(s_host, host);
    ui_label_set(s_error, error);
    if (lv_bar_get_max_value(s_bar) != (int32_t)total_s) {
        lv_bar_set_range(s_bar, 0, (int32_t)total_s);
    }
    if (lv_bar_get_value(s_bar) != (int32_t)remaining_s) {
        lv_bar_set_value(s_bar, (int32_t)remaining_s, LV_ANIM_OFF);
    }
    ui_unlock();
}
