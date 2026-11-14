#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <string.h>

#define BATT_X        20
#define BATT_W        20
#define BATT_H        10
#define BATT_Y        5
#define BATT_FILL_W   (BATT_W - 4)
#define TEXT_X        (BATT_X + BATT_W + 6)
#define RSSI_X        278
#define RSSI_BAR_W    3
#define RSSI_GAP      2
#define RSSI_BOTTOM   16
#define RSSI_BARS     4
#define DOT_SIZE      10
#define DOT_X         (LV_HOR_RES - DOT_SIZE - 4)

static lv_obj_t *s_bar;
static lv_obj_t *s_symbol;
static lv_obj_t *s_body;
static lv_obj_t *s_nub;
static lv_obj_t *s_fill;
static lv_obj_t *s_text;
static lv_obj_t *s_mode;
static lv_obj_t *s_rssi[RSSI_BARS];
static lv_obj_t *s_cross;
static lv_obj_t *s_dot;

/* Guarded by the ui lock. */
static ui_status_t s_status;
static bool s_valid;

static const lv_point_precise_t CROSS[] = {
    {RSSI_X - 1, 3},
    {RSSI_X + RSSI_BARS * (RSSI_BAR_W + RSSI_GAP), RSSI_BOTTOM + 1},
};

const char *ui_batt_name(ui_batt_t b)
{
    switch (b) {
    case UI_BATT_ABSENT:
        return "absent";
    case UI_BATT_DISCHARGING:
        return "discharging";
    case UI_BATT_CHARGING:
        return "charging";
    case UI_BATT_FULL:
        return "full";
    }
    return "";
}

const char *ui_link_name(ui_link_t l)
{
    switch (l) {
    case UI_LINK_REGISTERED:
        return "registered";
    case UI_LINK_CONNECTING:
        return "connecting";
    case UI_LINK_OFFLINE:
        return "offline";
    }
    return "";
}

static lv_obj_t *rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int32_t x)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, 0);
    lv_label_set_text_static(l, "");
    lv_obj_set_pos(l, x, 2);
    return l;
}

void ui_status_bar_create(void)
{
    s_bar = rect(lv_layer_top(), 0, 0, LV_HOR_RES, UI_STATUS_BAR_H);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_BAR_BG, 0);
    lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_border_color(s_bar, UI_COLOR_DIVIDER, 0);
    /* The screen command reports these values in statusBar; its texts list stays the screen's own. */
    lv_obj_add_flag(s_bar, UI_FLAG_NO_TEXT);

    s_symbol = label(s_bar, 4);
    s_body = rect(s_bar, BATT_X, BATT_Y, BATT_W, BATT_H);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 1, 0);
    lv_obj_set_style_radius(s_body, 2, 0);
    s_nub = rect(s_bar, BATT_X + BATT_W, BATT_Y + 3, 2, BATT_H - 6);
    s_fill = rect(s_bar, BATT_X + 2, BATT_Y + 2, BATT_FILL_W, BATT_H - 4);
    s_text = label(s_bar, TEXT_X);

    s_mode = label(s_bar, 0);
    lv_label_set_text_static(s_mode, "DESK");
    lv_obj_set_style_text_color(s_mode, UI_COLOR_ON_CHIP, 0);
    lv_obj_set_style_bg_color(s_mode, UI_COLOR_WARN, 0);
    lv_obj_set_style_bg_opa(s_mode, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_mode, 3, 0);
    lv_obj_set_style_pad_hor(s_mode, 6, 0);
    lv_obj_align(s_mode, LV_ALIGN_TOP_MID, 0, 1);

    for (int i = 0; i < RSSI_BARS; i++) {
        const int32_t h = 4 + i * 3;
        s_rssi[i] = rect(s_bar, RSSI_X + i * (RSSI_BAR_W + RSSI_GAP), RSSI_BOTTOM - h, RSSI_BAR_W, h);
    }
    s_cross = lv_line_create(s_bar);
    lv_line_set_points(s_cross, CROSS, 2);
    lv_obj_set_style_line_width(s_cross, 2, 0);
    lv_obj_set_style_line_color(s_cross, UI_COLOR_DANGER, 0);

    s_dot = rect(s_bar, DOT_X, (UI_STATUS_BAR_H - DOT_SIZE) / 2, DOT_SIZE, DOT_SIZE);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    s_valid = false;
}

void ui_status_bar_set_visible(bool visible)
{
    if (visible) {
        lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void draw_battery(const ui_status_t *s)
{
    const bool absent = s->battery == UI_BATT_ABSENT;
    const bool external = s->battery == UI_BATT_CHARGING || s->battery == UI_BATT_FULL;
    const lv_color_t color = s->low && !absent ? UI_COLOR_WARN : UI_COLOR_TEXT;

    set_hidden(s_body, absent);
    set_hidden(s_nub, absent);
    set_hidden(s_fill, absent);
    set_hidden(s_symbol, !absent && !external);
    lv_label_set_text_static(s_symbol, absent ? LV_SYMBOL_USB : LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(s_symbol, absent ? UI_COLOR_TEXT : UI_COLOR_OK, 0);
    lv_obj_set_style_border_color(s_body, color, 0);
    lv_obj_set_style_bg_color(s_nub, color, 0);
    lv_obj_set_style_bg_color(s_fill, color, 0);
    const uint8_t pct = s->pct > 100 ? 100 : s->pct;
    const int32_t w = (int32_t)pct * BATT_FILL_W / 100;
    lv_obj_set_width(s_fill, w > 0 ? w : 1);
    lv_obj_set_style_text_color(s_text, color, 0);
    if (absent) {
        lv_obj_set_x(s_text, BATT_X + 2);
        lv_label_set_text_static(s_text, "USB");
    } else {
        lv_obj_set_x(s_text, TEXT_X);
        lv_label_set_text_fmt(s_text, "%u%%", pct);
    }
}

static void draw_rssi(uint8_t bars)
{
    for (int i = 0; i < RSSI_BARS; i++) {
        lv_obj_set_style_bg_color(s_rssi[i], i < bars ? UI_COLOR_TEXT : UI_COLOR_INACTIVE, 0);
    }
    set_hidden(s_cross, bars != 0);
}

static void draw_link(ui_link_t link)
{
    lv_color_t c = UI_COLOR_DANGER;
    switch (link) {
    case UI_LINK_REGISTERED:
        c = UI_COLOR_OK;
        break;
    case UI_LINK_CONNECTING:
        c = UI_COLOR_WARN;
        break;
    case UI_LINK_OFFLINE:
        break;
    }
    lv_obj_set_style_bg_color(s_dot, c, 0);
}

void ui_status_set(const ui_status_t *s)
{
    if (!ui_lock()) {
        return;
    }
    if (!s_valid || s->battery != s_status.battery || s->pct != s_status.pct || s->low != s_status.low) {
        draw_battery(s);
    }
    if (!s_valid || s->desk != s_status.desk) {
        set_hidden(s_mode, !s->desk);
    }
    if (!s_valid || s->rssi_bars != s_status.rssi_bars) {
        draw_rssi(s->rssi_bars);
    }
    if (!s_valid || s->link != s_status.link) {
        draw_link(s->link);
    }
    s_status = *s;
    s_valid = true;
    ui_unlock();
}

void ui_status_get(ui_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (ui_lock()) {
        *out = s_status;
        ui_unlock();
    }
}
