#include "ui_priv.h"

#include "ui_theme.h"

#include <string.h>

#define CHIP_H       22
#define CHIP_PAD_L   7
#define CHIP_PAD_R   9
#define CHIP_GAP     5
#define CHIP_TEXT_W  150
#define BAR_W        4
#define BAR_STEP     5
#define BAR_H0       5
#define BAR_DH       4
#define BARS         3
#define BARS_W       ((BARS - 1) * BAR_STEP + BAR_W)
#define ROW_GAP      8
#define WORDS_GAP    6

struct w_chip {
    lv_obj_t *row;
    lv_obj_t *pill;
    lv_obj_t *icon;
    lv_obj_t *text;
    lv_obj_t *bars_box;
    lv_obj_t *bars[BARS];
    lv_obj_t *words;
    lv_color_t color;
    uint8_t lit;
    bool valid;
};

#define W_CHIP_MAX  4
static struct w_chip s_pool[W_CHIP_MAX];
static size_t s_used;

w_chip_t *w_chip_create(lv_obj_t *parent, int32_t x, int32_t y)
{
    if (s_used >= W_CHIP_MAX) {
        return NULL;
    }
    w_chip_t *c = &s_pool[s_used++];
    c->row = ui_box(parent, x, y, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_set_flex_flow(c->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c->row, ROW_GAP, 0);

    c->pill = ui_box(c->row, 0, 0, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_add_style(c->pill, &ui_style_chip, 0);
    lv_obj_set_style_pad_left(c->pill, CHIP_PAD_L, 0);
    lv_obj_set_style_pad_right(c->pill, CHIP_PAD_R, 0);
    lv_obj_set_style_pad_column(c->pill, CHIP_GAP, 0);
    lv_obj_set_flex_flow(c->pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->icon = ui_icon(c->pill, "", UI_C_INK, 0, 0);
    c->text = ui_text(c->pill, UI_FONT_SMALL, UI_C_INK, 0, 0, 0, true);
    lv_obj_set_style_max_width(c->text, CHIP_TEXT_W, 0);
    lv_label_set_long_mode(c->text, LV_LABEL_LONG_MODE_DOTS);

    c->bars_box = ui_box(c->row, 0, 0, BARS_W + WORDS_GAP, CHIP_H);
    for (int i = 0; i < BARS; i++) {
        const int32_t h = BAR_H0 + i * BAR_DH;
        c->bars[i] = ui_rect(c->bars_box, i * BAR_STEP, (CHIP_H + BAR_H0 + (BARS - 1) * BAR_DH) / 2 - h, BAR_W, h,
                             UI_C_LINE, 1);
    }
    c->words = ui_text(c->row, UI_FONT_SMALL, UI_C_CREAM, 0, 0, 0, true);
    lv_obj_add_style(c->words, &ui_style_caps, 0);
    /* The bars and the words sit closer together than the pill and the bars. */
    lv_obj_set_style_margin_left(c->words, WORDS_GAP - ROW_GAP, 0);
    c->valid = false;
    return c;
}

void w_chip_set(w_chip_t *c, const char *icon, const char *text, lv_color_t color, uint8_t bars, const char *words)
{
    if (c == NULL) {
        return;
    }
    if (bars > BARS) {
        bars = BARS;
    }
    if (strcmp(lv_label_get_text(c->icon), icon != NULL ? icon : "") != 0) {
        lv_label_set_text_static(c->icon, icon != NULL ? icon : "");
    }
    ui_label_set(c->text, text);
    if (!c->valid || !lv_color_eq(color, c->color) || bars != c->lit) {
        lv_obj_set_style_bg_color(c->pill, color, 0);
        for (int i = 0; i < BARS; i++) {
            lv_obj_set_style_bg_color(c->bars[i], i < bars ? color : UI_C_LINE, 0);
        }
        lv_obj_set_style_text_color(c->words, color, 0);
        c->color = color;
        c->lit = bars;
        c->valid = true;
    }
    ui_set_hidden(c->bars_box, bars == 0);
    ui_set_hidden(c->words, bars == 0);
    ui_label_set_caps(c->words, bars > 0 ? words : "");
}
