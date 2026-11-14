#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>

#define ROW_H         20
#define CHIP_MAX_W    150
#define SUMMARY_Y     50
#define SUMMARY_LINES 3
#define DETAILS_Y     116
#define DETAILS_SPACE 3
#define BAR_Y         214
#define BAR_H         4
#define HINT_Y        221
#define FRAME_W       2
#define BAR_WARN_PERMILLE  200

static lv_obj_t *s_chip;
static lv_obj_t *s_tool;
static lv_obj_t *s_position;
static lv_obj_t *s_project;
static lv_obj_t *s_summary;
static lv_obj_t *s_details;
static lv_obj_t *s_bar;
static lv_obj_t *s_reject;
static lv_obj_t *s_page;
static lv_obj_t *s_approve;
static lv_obj_t *s_frame;

/* Guarded by the ui lock. */
static bool s_valid;
static proto_risk_t s_risk;
static bool s_sent;
static bool s_bar_warn;

static lv_obj_t *hint(lv_obj_t *scr, lv_align_t align, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text_static(l, "");
    lv_obj_align(l, align, align == LV_ALIGN_TOP_LEFT ? UI_PAD : align == LV_ALIGN_TOP_RIGHT ? -UI_PAD : 0, HINT_Y);
    return l;
}

lv_obj_t *screen_approval_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_HOR_RES - 2 * UI_PAD, ROW_H);
    lv_obj_set_pos(row, UI_PAD, UI_TOP);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_PAD, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_chip = lv_obj_create(row);
    lv_obj_remove_style_all(s_chip);
    lv_obj_set_size(s_chip, LV_SIZE_CONTENT, ROW_H);
    lv_obj_set_style_bg_opa(s_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_chip, 4, 0);
    lv_obj_set_style_pad_hor(s_chip, 6, 0);
    lv_obj_remove_flag(s_chip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_tool = lv_label_create(s_chip);
    lv_obj_set_style_text_font(s_tool, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_tool, UI_COLOR_ON_CHIP, 0);
    lv_obj_set_style_max_width(s_tool, CHIP_MAX_W, 0);
    lv_label_set_long_mode(s_tool, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text_static(s_tool, "");
    lv_obj_center(s_tool);

    s_position = lv_label_create(row);
    lv_obj_set_style_text_font(s_position, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_position, UI_COLOR_TEXT, 0);
    lv_label_set_text_static(s_position, "");

    s_project = lv_label_create(row);
    lv_obj_set_style_text_font(s_project, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_project, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(s_project, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_flex_grow(s_project, 1);
    lv_obj_set_height(s_project, lv_font_get_line_height(UI_FONT_SMALL));
    lv_label_set_long_mode(s_project, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text_static(s_project, "");

    s_summary = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT, SUMMARY_Y, false);
    lv_obj_set_height(s_summary, SUMMARY_LINES * lv_font_get_line_height(UI_FONT_BODY));
    lv_label_set_long_mode(s_summary, LV_LABEL_LONG_MODE_DOTS);

    /* The lines arrive wrapped to UI_DETAILS_COLS, which fit the width, so LVGL never wraps them again. */
    s_details = ui_label_create(scr, UI_FONT_MONO, UI_COLOR_TEXT_DIM, DETAILS_Y, false);
    lv_obj_set_style_text_line_space(s_details, DETAILS_SPACE, 0);
    lv_label_set_long_mode(s_details, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_height(s_details, UI_DETAILS_LINES * (lv_font_get_line_height(UI_FONT_MONO) + DETAILS_SPACE));

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, LV_HOR_RES - 2 * UI_PAD, BAR_H);
    lv_obj_align(s_bar, LV_ALIGN_TOP_LEFT, UI_PAD, BAR_Y);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(s_bar, 0, 0);

    s_reject = hint(scr, LV_ALIGN_TOP_LEFT, UI_COLOR_TEXT);
    s_page = hint(scr, LV_ALIGN_TOP_MID, UI_COLOR_TEXT_DIM);
    s_approve = hint(scr, LV_ALIGN_TOP_RIGHT, UI_COLOR_OK);

    s_frame = lv_obj_create(scr);
    lv_obj_remove_style_all(s_frame);
    lv_obj_set_pos(s_frame, 0, UI_STATUS_BAR_H);
    lv_obj_set_size(s_frame, LV_HOR_RES, LV_VER_RES - UI_STATUS_BAR_H);
    lv_obj_set_style_border_width(s_frame, FRAME_W, 0);
    lv_obj_set_style_border_color(s_frame, UI_COLOR_RISK_HIGH, 0);
    lv_obj_remove_flag(s_frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
    s_valid = false;
    return scr;
}

static lv_color_t risk_color(proto_risk_t risk)
{
    switch (risk) {
    case PROTO_RISK_LOW:
        return UI_COLOR_RISK_LOW;
    case PROTO_RISK_MED:
        return UI_COLOR_RISK_MED;
    case PROTO_RISK_HIGH:
        break;
    }
    return UI_COLOR_RISK_HIGH;
}

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (hidden != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (hidden) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_approval_set(const ui_approval_t *a)
{
    if (!ui_lock()) {
        return;
    }
    const bool high = a->risk == PROTO_RISK_HIGH;
    if (!s_valid || a->risk != s_risk || a->sent != s_sent) {
        lv_obj_set_style_bg_color(s_chip, risk_color(a->risk), 0);
        set_hidden(s_frame, !high);
        ui_label_set(s_reject, a->sent ? "" : "REJECT");
        ui_label_set(s_approve, a->sent ? "" : high ? "HOLD TO APPROVE" : "APPROVE");
        lv_obj_set_style_text_color(s_approve, high ? UI_COLOR_RISK_HIGH : UI_COLOR_OK, 0);
        lv_obj_set_style_text_color(s_page, a->sent ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM, 0);
        s_risk = a->risk;
        s_sent = a->sent;
        s_valid = true;
    }
    ui_label_set(s_tool, a->tool);
    char line[24] = "";
    if (a->count > 1) {
        snprintf(line, sizeof(line), "%u/%u", a->position, a->count);
    }
    ui_label_set(s_position, line);
    set_hidden(s_position, a->count <= 1);
    ui_label_set(s_project, a->project);
    ui_label_set(s_summary, a->summary);
    ui_label_set(s_details, a->pages > 0 ? a->details : "");
    if (a->sent) {
        ui_label_set(s_page, "Sent...");
    } else if (a->pages > 1) {
        snprintf(line, sizeof(line), "%u/%u", a->page, a->pages);
        ui_label_set(s_page, line);
    } else {
        ui_label_set(s_page, "");
    }
    const int32_t value = a->remaining_permille > 1000 ? 1000 : a->remaining_permille;
    if (lv_bar_get_value(s_bar) != value) {
        lv_bar_set_value(s_bar, value, LV_ANIM_OFF);
    }
    const bool warn = value <= BAR_WARN_PERMILLE;
    if (warn != s_bar_warn) {
        s_bar_warn = warn;
        lv_obj_set_style_bg_color(s_bar, warn ? UI_COLOR_WARN : UI_COLOR_ACCENT, LV_PART_INDICATOR);
    }
    ui_unlock();
}
