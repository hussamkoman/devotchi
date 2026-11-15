#include "ui_priv.h"

#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#define TOAST_X         12
#define TOAST_W         (LV_HOR_RES - 2 * TOAST_X)
#define TOAST_Y         186
#define STICKY_Y        170
#define TOAST_H         34
#define TOAST_H_DETAIL  46
#define CIRCLE          26
#define TEXT_W          236
#define STRIP_H         21

typedef struct {
    lv_obj_t *box;
    lv_obj_t *circle;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *detail;
} toast_obj_t;

static toast_obj_t s_toast;
static toast_obj_t s_banner;
static lv_timer_t *s_toast_timer;
static lv_obj_t *s_strip;
static lv_obj_t *s_strip_icon;
static lv_obj_t *s_strip_text;
static lv_obj_t *s_strip_right;
static lv_timer_t *s_strip_timer;

/* The app's banner, kept while ui demo has the screen. Guarded by the ui lock. */
static char s_app_banner[UI_TEXT_SIZE];
static bool s_app_banner_attention;
static bool s_app_banner_shown;

static void toast_expired(lv_timer_t *t);
static void strip_expired(lv_timer_t *t);

static void make(toast_obj_t *t, int32_t y)
{
    t->box = ui_box(lv_layer_top(), TOAST_X, y, TOAST_W, TOAST_H);
    lv_obj_add_style(t->box, &ui_style_toast, 0);
    lv_obj_set_style_pad_hor(t->box, 10, 0);
    lv_obj_set_style_pad_column(t->box, 10, 0);
    lv_obj_set_flex_flow(t->box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(t->box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* Reported as toast and banner by the screen command. */
    lv_obj_add_flag(t->box, UI_FLAG_NO_TEXT);
    t->circle = ui_rect(t->box, 0, 0, CIRCLE, CIRCLE, UI_C_FOG, LV_RADIUS_CIRCLE);
    t->icon = ui_icon(t->circle, UI_ICON_INFO, UI_C_INK, 0, 0);
    lv_obj_center(t->icon);
    lv_obj_t *col = ui_box(t->box, 0, 0, TEXT_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    t->title = ui_text(col, UI_FONT_BODY, UI_C_CREAM, 0, 0, TEXT_W, true);
    t->detail = ui_text(col, UI_FONT_SMALL, UI_C_FOG, 0, 0, TEXT_W, true);
    lv_obj_add_flag(t->box, LV_OBJ_FLAG_HIDDEN);
}

void w_toast_create(void)
{
    make(&s_banner, STICKY_Y);
    make(&s_toast, TOAST_Y);
    s_toast_timer = lv_timer_create(toast_expired, UI_TOAST_DEFAULT_MS, NULL);
    lv_timer_pause(s_toast_timer);

    s_strip = ui_rect(lv_layer_top(), 0, 0, LV_HOR_RES, STRIP_H, UI_C_SURFACE2, 0);
    lv_obj_add_flag(s_strip, UI_FLAG_NO_TEXT);
    s_strip_icon = ui_icon(s_strip, UI_ICON_X, UI_C_CORAL, 10, 2);
    s_strip_text = ui_text(s_strip, UI_FONT_SMALL, UI_C_CREAM, 31, 1, 230, true);
    s_strip_right = ui_text(s_strip, UI_FONT_SMALL, UI_C_FOG, 262, 1, 48, true);
    lv_obj_set_style_text_align(s_strip_right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
    s_strip_timer = lv_timer_create(strip_expired, 1000, NULL);
    lv_timer_pause(s_strip_timer);
}

static void kind_style(ui_toast_kind_t kind, const char **icon, lv_color_t *color)
{
    switch (kind) {
    case UI_TOAST_POSITIVE:
        *icon = UI_ICON_CHECK;
        *color = UI_C_MINT;
        return;
    case UI_TOAST_NEGATIVE:
        *icon = UI_ICON_X;
        *color = UI_C_CORAL;
        return;
    case UI_TOAST_ATTENTION:
        *icon = UI_ICON_BELL;
        *color = UI_C_AMBER;
        return;
    case UI_TOAST_DESK:
        *icon = UI_ICON_DESK;
        *color = UI_C_FOG;
        return;
    case UI_TOAST_BATTERY:
        *icon = UI_ICON_BOLT;
        *color = UI_C_AMBER;
        return;
    case UI_TOAST_INFO:
        break;
    }
    *icon = UI_ICON_INFO;
    *color = UI_C_FOG;
}

static void anim_translate_y(void *var, int32_t v)
{
    lv_obj_set_style_translate_y(var, v, 0);
}

static void slide_out_done(lv_anim_t *a)
{
    lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_y(a->var, 0, 0);
}

/* Slides the box up from the bottom edge (in) or back down and hides it (out). */
static void slide(lv_obj_t *box, bool in)
{
    const int32_t away = LV_VER_RES - lv_obj_get_y(box);
    lv_anim_delete(box, anim_translate_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_exec_cb(&a, anim_translate_y);
    lv_anim_set_values(&a, in ? away : 0, in ? 0 : away);
    lv_anim_set_duration(&a, UI_MS_MOVE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (!in) {
        lv_anim_set_completed_cb(&a, slide_out_done);
    }
    lv_anim_start(&a);
}

static void fill(toast_obj_t *t, ui_toast_kind_t kind, const char *title, const char *detail)
{
    const char *icon;
    lv_color_t color;
    kind_style(kind, &icon, &color);
    lv_obj_set_style_bg_color(t->circle, color, 0);
    lv_label_set_text_static(t->icon, icon);
    ui_label_set(t->title, title);
    const bool has_detail = detail != NULL && detail[0] != '\0';
    ui_label_set(t->detail, has_detail ? detail : "");
    ui_set_hidden(t->detail, !has_detail);
    lv_obj_set_height(t->box, has_detail ? TOAST_H_DETAIL : TOAST_H);
}

/* Caller holds the ui lock. */
static void show_toast(ui_toast_kind_t kind, const char *title, const char *detail, uint32_t ms)
{
    const bool was_hidden = lv_obj_has_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN);
    fill(&s_toast, kind, title, detail);
    lv_obj_remove_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN);
    if (was_hidden) {
        slide(s_toast.box, true);
    } else {
        lv_anim_delete(s_toast.box, anim_translate_y);
        lv_obj_set_style_translate_y(s_toast.box, 0, 0);
    }
    if (ms == 0) {
        lv_timer_pause(s_toast_timer);
        return;
    }
    lv_timer_set_period(s_toast_timer, ms);
    lv_timer_reset(s_toast_timer);
    lv_timer_resume(s_toast_timer);
}

static void toast_expired(lv_timer_t *t)
{
    lv_timer_pause(t);
    slide(s_toast.box, false);
}

static void strip_expired(lv_timer_t *t)
{
    lv_timer_pause(t);
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
}

void ui_toast_show(ui_toast_kind_t kind, const char *title, const char *detail, uint32_t ms)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        show_toast(kind, title, detail, ms != 0 ? ms : UI_TOAST_DEFAULT_MS);
    }
    ui_unlock();
}

void ui_toast(const char *text, uint32_t ms)
{
    ui_toast_show(UI_TOAST_INFO, text, NULL, ms);
}

/* Caller holds the ui lock. */
static void draw_banner(void)
{
    const bool was_hidden = lv_obj_has_flag(s_banner.box, LV_OBJ_FLAG_HIDDEN);
    if (!s_app_banner_shown) {
        if (!was_hidden) {
            lv_anim_delete(s_banner.box, anim_translate_y);
            lv_obj_set_style_translate_y(s_banner.box, 0, 0);
            lv_obj_add_flag(s_banner.box, LV_OBJ_FLAG_HIDDEN);
        }
        screen_pet_set_attention(false);
        return;
    }
    if (s_app_banner_attention) {
        fill(&s_banner, UI_TOAST_ATTENTION, s_app_banner, NULL);
    } else {
        fill(&s_banner, UI_TOAST_INFO, s_app_banner, NULL);
        lv_obj_set_style_bg_color(s_banner.circle, UI_C_CORAL, 0);
        lv_label_set_text_static(s_banner.icon, UI_ICON_WIFI);
    }
    lv_obj_remove_flag(s_banner.box, LV_OBJ_FLAG_HIDDEN);
    if (was_hidden) {
        slide(s_banner.box, true);
    }
    screen_pet_set_attention(s_app_banner_attention);
}

void ui_banner_show(const char *text, bool attention)
{
    if (!ui_lock()) {
        return;
    }
    strlcpy(s_app_banner, text != NULL ? text : "", sizeof(s_app_banner));
    s_app_banner_attention = attention;
    s_app_banner_shown = true;
    if (!ui_demo_active()) {
        draw_banner();
    }
    ui_unlock();
}

void ui_banner_hide(void)
{
    if (!ui_lock()) {
        return;
    }
    s_app_banner_shown = false;
    if (!ui_demo_active()) {
        draw_banner();
    }
    ui_unlock();
}

void w_toast_strip(const char *icon, lv_color_t color, const char *text, const char *right, uint32_t ms)
{
    lv_label_set_text_static(s_strip_icon, icon);
    lv_obj_set_style_text_color(s_strip_icon, color, 0);
    ui_label_set(s_strip_text, text);
    ui_label_set(s_strip_right, right);
    lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
    if (ms == 0) {
        lv_timer_pause(s_strip_timer);
        return;
    }
    lv_timer_set_period(s_strip_timer, ms);
    lv_timer_reset(s_strip_timer);
    lv_timer_resume(s_strip_timer);
}

void w_toast_demo(w_toast_demo_t demo)
{
    lv_timer_pause(s_toast_timer);
    lv_anim_delete(s_toast.box, anim_translate_y);
    lv_obj_set_style_translate_y(s_toast.box, 0, 0);
    lv_obj_add_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_banner.box, LV_OBJ_FLAG_HIDDEN);
    strip_expired(s_strip_timer);
    switch (demo) {
    case W_TOAST_DEMO_NONE:
        /* A demo screen starts without the app's banner; ui demo off brings it back. */
        if (ui_demo_active()) {
            screen_pet_set_attention(false);
        } else {
            draw_banner();
        }
        return;
    case W_TOAST_DEMO_POSITIVE:
        fill(&s_toast, UI_TOAST_POSITIVE, "Tests passed", "47 of 47 \xC2\xB7 packages/server");
        lv_obj_remove_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN);
        return;
    case W_TOAST_DEMO_STICKY:
        fill(&s_banner, UI_TOAST_NEGATIVE, "Build failed", "tsc: 3 errors in src/ws.ts");
        lv_obj_remove_flag(s_banner.box, LV_OBJ_FLAG_HIDDEN);
        screen_pet_set_attention(true);
        return;
    case W_TOAST_DEMO_DESK:
        fill(&s_toast, UI_TOAST_DESK, "Desk mode on", "Requests go to your terminal");
        lv_obj_remove_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN);
        return;
    case W_TOAST_DEMO_BATTERY:
        fill(&s_toast, UI_TOAST_BATTERY, "Battery low \xC2\xB7 12 %", "Plug in USB-C soon");
        lv_obj_remove_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN);
        return;
    case W_TOAST_DEMO_STRIP:
        w_toast_strip(UI_ICON_X, UI_C_CORAL, "Denied \xC2\xB7 next request", "", 0);
        return;
    }
}

void ui_overlay_get(char toast[UI_TEXT_SIZE], char banner[UI_TEXT_SIZE])
{
    toast[0] = '\0';
    banner[0] = '\0';
    if (!ui_lock()) {
        return;
    }
    if (!lv_obj_has_flag(s_toast.box, LV_OBJ_FLAG_HIDDEN)) {
        strlcpy(toast, lv_label_get_text(s_toast.title), UI_TEXT_SIZE);
    }
    if (!lv_obj_has_flag(s_banner.box, LV_OBJ_FLAG_HIDDEN)) {
        strlcpy(banner, lv_label_get_text(s_banner.title), UI_TEXT_SIZE);
    }
    ui_unlock();
}
