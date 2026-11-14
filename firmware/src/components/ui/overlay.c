#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <string.h>

#define TOAST_H       26
#define TOAST_MAX_W   (LV_HOR_RES - 2 * UI_PAD)
#define BANNER_H      24

static lv_obj_t *s_toast;
static lv_obj_t *s_toast_text;
static lv_timer_t *s_toast_timer;
static lv_obj_t *s_banner;
static lv_obj_t *s_banner_text;

static void toast_expired(lv_timer_t *t)
{
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}

static lv_obj_t *box(lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    /* Reported as toast and banner by the screen command. */
    lv_obj_add_flag(o, UI_FLAG_NO_TEXT);
    return o;
}

static lv_obj_t *one_line(lv_obj_t *parent, lv_color_t color, int32_t width)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_width(l, width);
    lv_obj_set_height(l, lv_font_get_line_height(UI_FONT_SMALL));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(l, "");
    lv_obj_center(l);
    return l;
}

void ui_overlay_create(void)
{
    s_toast = box(UI_COLOR_TOAST_BG);
    lv_obj_set_size(s_toast, TOAST_MAX_W, TOAST_H);
    lv_obj_set_style_radius(s_toast, 6, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, UI_COLOR_DIVIDER, 0);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -4);
    s_toast_text = one_line(s_toast, UI_COLOR_TEXT, TOAST_MAX_W - 2 * UI_PAD);

    s_toast_timer = lv_timer_create(toast_expired, UI_TOAST_DEFAULT_MS, NULL);
    lv_timer_pause(s_toast_timer);

    s_banner = box(UI_COLOR_WARN);
    lv_obj_set_size(s_banner, LV_HOR_RES, BANNER_H);
    lv_obj_set_pos(s_banner, 0, UI_STATUS_BAR_H);
    s_banner_text = one_line(s_banner, UI_COLOR_ON_CHIP, LV_HOR_RES - 2 * UI_PAD);
}

void ui_toast(const char *text, uint32_t ms)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_toast_text, text);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(s_toast_timer, ms != 0 ? ms : UI_TOAST_DEFAULT_MS);
    lv_timer_reset(s_toast_timer);
    lv_timer_resume(s_toast_timer);
    ui_unlock();
}

void ui_banner_show(const char *text)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_banner_text, text);
    lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    ui_unlock();
}

void ui_banner_hide(void)
{
    if (!ui_lock()) {
        return;
    }
    if (!lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    }
    ui_unlock();
}

void ui_overlay_get(char toast[UI_TEXT_SIZE], char banner[UI_TEXT_SIZE])
{
    toast[0] = '\0';
    banner[0] = '\0';
    if (!ui_lock()) {
        return;
    }
    if (!lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN)) {
        strlcpy(toast, lv_label_get_text(s_toast_text), UI_TEXT_SIZE);
    }
    if (!lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN)) {
        strlcpy(banner, lv_label_get_text(s_banner_text), UI_TEXT_SIZE);
    }
    ui_unlock();
}
