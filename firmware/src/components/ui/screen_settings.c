#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#define ROW_H        19
#define ROW_RADIUS   4
#define ROW_INSET    6
#define HINT_Y       (UI_TOP + UI_SETTINGS_ROWS * ROW_H + 3)
#define CONFIRM_TEXT "Press ACTION again to confirm"

static lv_obj_t *s_rows[UI_SETTINGS_ROWS];
static lv_obj_t *s_labels[UI_SETTINGS_ROWS];
static lv_obj_t *s_values[UI_SETTINGS_ROWS];

/* Guarded by the ui lock. */
static bool s_valid;
static uint8_t s_cursor;
static bool s_confirming;

static lv_obj_t *row_label(lv_obj_t *row, lv_align_t align)
{
    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text_static(l, "");
    lv_obj_align(l, align, align == LV_ALIGN_LEFT_MID ? ROW_INSET : -ROW_INSET, 0);
    return l;
}

lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    for (int i = 0; i < UI_SETTINGS_ROWS; i++) {
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_HOR_RES - 2 * UI_PAD, ROW_H);
        lv_obj_set_pos(row, UI_PAD, UI_TOP + i * ROW_H);
        lv_obj_set_style_radius(row, ROW_RADIUS, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_rows[i] = row;
        s_labels[i] = row_label(row, LV_ALIGN_LEFT_MID);
        s_values[i] = row_label(row, LV_ALIGN_RIGHT_MID);
    }
    lv_obj_t *hint = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, HINT_Y, true);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(hint, LV_SYMBOL_UP " REJECT   " LV_SYMBOL_DOWN " APPROVE   " LV_SYMBOL_LEFT " hold ACTION");
    s_valid = false;
    return scr;
}

static void style_row(int i, bool selected, bool confirming)
{
    lv_obj_set_style_bg_opa(s_rows[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_rows[i], confirming ? UI_COLOR_DANGER : UI_COLOR_ACCENT, 0);
    const lv_color_t text = selected ? UI_COLOR_ON_CHIP : UI_COLOR_TEXT;
    lv_obj_set_style_text_color(s_labels[i], text, 0);
    lv_obj_set_style_text_color(s_values[i], selected ? UI_COLOR_ON_CHIP : UI_COLOR_TEXT_DIM, 0);
}

void ui_settings_set(const ui_settings_t *s)
{
    if (!ui_lock()) {
        return;
    }
    const uint8_t cursor = s->cursor < UI_SETTINGS_ROWS ? s->cursor : 0;
    if (!s_valid || cursor != s_cursor || s->confirming != s_confirming) {
        if (s_valid) {
            style_row(s_cursor, false, false);
        } else {
            for (int i = 0; i < UI_SETTINGS_ROWS; i++) {
                style_row(i, false, false);
            }
        }
        style_row(cursor, true, s->confirming);
        s_valid = true;
        s_cursor = cursor;
        s_confirming = s->confirming;
    }
    for (int i = 0; i < UI_SETTINGS_ROWS; i++) {
        const bool confirm = s->confirming && i == cursor;
        ui_label_set(s_labels[i], confirm ? CONFIRM_TEXT : s->labels[i]);
        ui_label_set(s_values[i], confirm ? "" : s->values[i]);
    }
    ui_unlock();
}
