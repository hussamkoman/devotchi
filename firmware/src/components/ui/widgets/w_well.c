#include "ui_priv.h"

#include "ui_theme.h"

#define PAD_V     8
#define PAD_H     10
#define LINE_H    18
#define FOOT_H    20
#define EDGE_W    3
#define TEXT_W    (UI_WIDTH_SURFACE - 2 * PAD_H)

struct w_well {
    lv_obj_t *box;
    lv_obj_t *text;
    lv_obj_t *prompt;
    lv_obj_t *foot_left;
    lv_obj_t *foot_right;
    int32_t height;
    bool edge;
    lv_color_t edge_color;
    bool valid;
};

#define W_WELL_MAX  4
static struct w_well s_pool[W_WELL_MAX];
static size_t s_used;

w_well_t *w_well_create(lv_obj_t *parent, int32_t y)
{
    if (s_used >= W_WELL_MAX) {
        return NULL;
    }
    w_well_t *w = &s_pool[s_used++];
    w->box = ui_box(parent, UI_PAD, y, UI_WIDTH_SURFACE, 2 * PAD_V + LINE_H);
    lv_obj_add_style(w->box, &ui_style_well, 0);
    lv_obj_set_style_border_side(w->box, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(w->box, 0, 0);
    /* The lines arrive wrapped to the well's columns, so LVGL never wraps them again. */
    w->text = ui_text(w->box, UI_FONT_MONO, UI_C_CREAM, PAD_H, PAD_V, TEXT_W, false);
    lv_label_set_long_mode(w->text, LV_LABEL_LONG_MODE_CLIP);
    w->prompt = ui_text(w->box, UI_FONT_MONO, UI_C_SLATE, PAD_H, PAD_V, 0, true);
    lv_label_set_text_static(w->prompt, "$");
    lv_obj_add_flag(w->prompt, LV_OBJ_FLAG_HIDDEN);
    w->foot_left = ui_text(w->box, UI_FONT_SMALL, UI_C_FOG, PAD_H, 0, TEXT_W - 60, true);
    w->foot_right = ui_text(w->box, UI_FONT_MONO, UI_C_FOG, UI_WIDTH_SURFACE - PAD_H - 60, 0, 60, true);
    lv_obj_set_style_text_align(w->foot_right, LV_TEXT_ALIGN_RIGHT, 0);
    w->valid = false;
    return w;
}

void w_well_set_prompt(w_well_t *w, bool on)
{
    if (w != NULL) {
        ui_set_hidden(w->prompt, !on);
    }
}

lv_obj_t *w_well_obj(w_well_t *w)
{
    return w->box;
}

int32_t w_well_set(w_well_t *w, const char *text, uint8_t lines, const char *foot_left, const char *foot_right,
                   bool edge, lv_color_t edge_color)
{
    if (w == NULL) {
        return 0;
    }
    if (lines == 0) {
        lines = 1;
    }
    const bool foot = foot_left != NULL || foot_right != NULL;
    const int32_t text_h = (int32_t)lines * LINE_H;
    const int32_t height = 2 * PAD_V + text_h + (foot ? FOOT_H : 0);
    ui_label_set(w->text, text);
    if (!w->valid || height != w->height) {
        lv_obj_set_height(w->text, text_h);
        lv_obj_set_y(w->foot_left, PAD_V + text_h + 2);
        lv_obj_set_y(w->foot_right, PAD_V + text_h + 2);
        lv_obj_set_height(w->box, height);
        w->height = height;
    }
    ui_set_hidden(w->foot_left, !foot);
    ui_set_hidden(w->foot_right, !foot);
    ui_label_set(w->foot_left, foot_left);
    ui_label_set(w->foot_right, foot_right);
    if (!w->valid || edge != w->edge || !lv_color_eq(edge_color, w->edge_color)) {
        lv_obj_set_style_border_width(w->box, edge ? EDGE_W : 0, 0);
        lv_obj_set_style_border_color(w->box, edge_color, 0);
        w->edge = edge;
        w->edge_color = edge_color;
    }
    w->valid = true;
    return height;
}
