#include "ui_priv.h"

#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <string.h>

#define LEFT_X        10
#define LEFT_GAP      7
#define TAG_H         18
#define DOT_X         245
#define DOT_SIZE      7
#define BARS_X        259
#define BAR_W         3
#define BAR_STEP      5
#define BAR_BOTTOM    15
#define BARS          4
#define BATT_X        288
#define BATT_Y        5
#define BATT_W        21
#define BATT_H        11
#define BATT_BORDER   2
#define BATT_FILL_W   (BATT_W - 2 * BATT_BORDER - 1)
#define BATT_LOW_PCT  15

static lv_obj_t *s_bar;
static lv_obj_t *s_left;
static lv_obj_t *s_time;
static lv_obj_t *s_title;
static lv_obj_t *s_desk;
static lv_obj_t *s_dot;
static lv_obj_t *s_bars[BARS];
static lv_obj_t *s_cross;
static lv_obj_t *s_batt;
static lv_obj_t *s_nub;
static lv_obj_t *s_fill;
static lv_obj_t *s_bolt;
static lv_obj_t *s_plug;

/* Guarded by the ui lock. */
static ui_status_t s_status;
static ui_status_t s_drawn;
static bool s_valid;
static bool s_demo;

static const lv_point_precise_t CROSS[] = {{BARS_X - 1, 4}, {BARS_X + BARS * BAR_STEP, 16}};

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

void w_status_create(void)
{
    s_bar = ui_box(lv_layer_top(), 0, 0, LV_HOR_RES, UI_STATUS_BAR_H);
    /* The screen command reports these values in statusBar; its texts list stays the screen's own. */
    lv_obj_add_flag(s_bar, UI_FLAG_NO_TEXT);

    s_left = ui_box(s_bar, LEFT_X, 1, LV_SIZE_CONTENT, TAG_H);
    lv_obj_set_flex_flow(s_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_left, LEFT_GAP, 0);
    s_time = ui_text(s_left, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);
    s_title = ui_text(s_left, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);
    lv_obj_add_style(s_title, &ui_style_caps, 0);
    s_desk = lv_label_create(s_left);
    lv_obj_add_style(s_desk, &ui_style_caps, 0);
    lv_obj_set_height(s_desk, TAG_H);
    lv_obj_set_style_pad_hor(s_desk, 5, 0);
    lv_obj_set_style_border_width(s_desk, 1, 0);
    lv_obj_set_style_border_color(s_desk, UI_C_SLATE, 0);
    lv_obj_set_style_radius(s_desk, UI_R_TAG, 0);
    lv_label_set_text_static(s_desk, "DESK");

    s_dot = ui_rect(s_bar, DOT_X, (UI_STATUS_BAR_H - DOT_SIZE) / 2, DOT_SIZE, DOT_SIZE, UI_C_CORAL, LV_RADIUS_CIRCLE);
    for (int i = 0; i < BARS; i++) {
        const int32_t h = 3 + (i * 5 + 1) / 2;
        s_bars[i] = ui_rect(s_bar, BARS_X + i * BAR_STEP, BAR_BOTTOM - h, BAR_W, h, UI_C_LINE, 1);
    }
    s_cross = lv_line_create(s_bar);
    lv_line_set_points(s_cross, CROSS, 2);
    lv_obj_set_style_line_width(s_cross, 2, 0);
    lv_obj_set_style_line_rounded(s_cross, true, 0);
    lv_obj_set_style_line_color(s_cross, UI_C_CORAL, 0);

    s_batt = ui_box(s_bar, BATT_X, BATT_Y, BATT_W, BATT_H);
    lv_obj_set_style_border_width(s_batt, BATT_BORDER, 0);
    lv_obj_set_style_radius(s_batt, 3, 0);
    s_nub = ui_rect(s_bar, BATT_X + BATT_W, BATT_Y + 3, 2, BATT_H - 6, UI_C_FOG, 1);
    s_fill = ui_rect(s_bar, BATT_X + BATT_BORDER + 1, BATT_Y + BATT_BORDER + 1, BATT_FILL_W,
                     BATT_H - 2 * (BATT_BORDER + 1), UI_C_FOG, 1);
    s_bolt = ui_icon(s_bar, UI_ICON_BOLT, UI_C_CREAM, BATT_X + 3, 2);
    s_plug = ui_icon(s_bar, UI_ICON_PLUG, UI_C_FOG, BATT_X + 3, 2);
    lv_label_set_text_static(s_time, "");
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    s_valid = false;
}

void w_status_set_visible(bool visible)
{
    ui_set_hidden(s_bar, !visible);
}

void w_status_set_title(const char *title)
{
    const bool has = title != NULL && title[0] != '\0';
    ui_label_set_caps(s_title, has ? title : "");
    ui_set_hidden(s_title, !has);
    ui_set_hidden(s_time, has);
}

static void draw_battery(const ui_status_t *s)
{
    const bool absent = s->battery == UI_BATT_ABSENT;
    const bool external = s->battery == UI_BATT_CHARGING || s->battery == UI_BATT_FULL;
    const uint8_t pct = s->pct > 100 ? 100 : s->pct;
    const lv_color_t color = !absent && (s->low || pct <= BATT_LOW_PCT) && !external ? UI_C_CORAL : UI_C_FOG;

    ui_set_hidden(s_batt, absent);
    ui_set_hidden(s_nub, absent);
    ui_set_hidden(s_fill, absent);
    ui_set_hidden(s_bolt, absent || !external);
    ui_set_hidden(s_plug, !absent);
    lv_obj_set_style_border_color(s_batt, color, 0);
    lv_obj_set_style_bg_color(s_nub, color, 0);
    lv_obj_set_style_bg_color(s_fill, external ? UI_C_MINT : color, 0);
    const int32_t w = (int32_t)pct * BATT_FILL_W / 100;
    lv_obj_set_width(s_fill, w > 2 ? w : 2);
}

static void draw_bars(uint8_t bars)
{
    for (int i = 0; i < BARS; i++) {
        lv_obj_set_style_bg_color(s_bars[i], i < bars ? UI_C_FOG : UI_C_LINE, 0);
    }
    ui_set_hidden(s_cross, bars != 0);
}

static void draw_link(ui_link_t link)
{
    lv_color_t c = UI_C_CORAL;
    if (link == UI_LINK_REGISTERED) {
        c = UI_C_MINT;
    } else if (link == UI_LINK_CONNECTING) {
        c = UI_C_AMBER;
    }
    lv_obj_set_style_bg_color(s_dot, c, 0);
}

/* Caller holds the ui lock; touches only what changed since the last draw. */
static void draw(const ui_status_t *s)
{
    if (!s_valid || s->battery != s_drawn.battery || s->pct != s_drawn.pct || s->low != s_drawn.low) {
        draw_battery(s);
    }
    if (!s_valid || s->desk != s_drawn.desk) {
        ui_set_hidden(s_desk, !s->desk);
    }
    if (!s_valid || s->rssi_bars != s_drawn.rssi_bars) {
        draw_bars(s->rssi_bars);
    }
    if (!s_valid || s->link != s_drawn.link) {
        draw_link(s->link);
    }
    s_drawn = *s;
    s_valid = true;
}

void ui_status_set(const ui_status_t *s)
{
    if (!ui_lock()) {
        return;
    }
    s_status = *s;
    if (!s_demo) {
        draw(s);
    }
    ui_unlock();
}

void w_status_demo(const ui_status_t *st)
{
    s_demo = st != NULL;
    draw(st != NULL ? st : &s_status);
}

void ui_status_get(ui_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (ui_lock()) {
        *out = s_demo ? s_drawn : s_status;
        ui_unlock();
    }
}
