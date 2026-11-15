#include "ui_priv.h"

#include "ui_theme.h"

#include <string.h>

#define ROW_GAP      2
#define PAD_H        10
#define ICON_W       14
#define ICON_GAP     8
#define DESC_LINE_H  17
#define DESC_LINES   3
#define DESC_PAD     5
#define VALUE_W      110
#define THUMB_W      3
#define THUMB_GAP    5

typedef struct {
    lv_obj_t *box;
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_obj_t *value;
} row_obj_t;

struct w_list {
    lv_obj_t *clip;
    lv_obj_t *content;
    lv_obj_t *focus;
    lv_obj_t *track;
    lv_obj_t *thumb;
    /* One description, under whichever row has the focus. */
    lv_obj_t *desc;
    row_obj_t rows[W_LIST_MAX_ROWS];
    uint8_t made;
    int32_t h;
    int32_t row_h;
    uint8_t count;
    uint8_t focused;
    uint8_t top;
    w_list_focus_t style;
    int32_t width;
    uint32_t hash;
    bool valid;
};

#define W_LIST_MAX  2
static struct w_list s_pool[W_LIST_MAX];
static size_t s_used;

w_list_t *w_list_create(lv_obj_t *parent, int32_t y, int32_t h, int32_t row_h)
{
    if (s_used >= W_LIST_MAX) {
        return NULL;
    }
    w_list_t *l = &s_pool[s_used++];
    l->h = h;
    l->row_h = row_h;
    l->clip = ui_box(parent, UI_PAD, y, UI_WIDTH_SURFACE, h);
    l->content = ui_box(l->clip, 0, 0, UI_WIDTH_SURFACE, LV_SIZE_CONTENT);
    l->focus = ui_box(l->content, 0, 0, UI_WIDTH_SURFACE, row_h);
    lv_obj_add_style(l->focus, &ui_style_focus, 0);
    l->desc = ui_text(l->content, UI_FONT_SMALL, UI_C_SUB_ON_CREAM, PAD_H, 0, 0, false);
    lv_obj_set_style_text_line_space(l->desc, DESC_LINE_H - lv_font_get_line_height(UI_FONT_SMALL), 0);
    lv_label_set_long_mode(l->desc, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_add_flag(l->desc, LV_OBJ_FLAG_HIDDEN);
    l->track = ui_rect(l->clip, UI_WIDTH_SURFACE - THUMB_W, 0, THUMB_W, h, UI_C_SURFACE2, 2);
    l->thumb = ui_rect(l->track, 0, 0, THUMB_W, h, UI_C_FOG, 2);
    l->valid = false;
    return l;
}

/* Rows are made the first time a list needs them and kept. */
static void make_rows(w_list_t *l, uint8_t count)
{
    for (; l->made < count; l->made++) {
        row_obj_t *r = &l->rows[l->made];
        r->box = ui_box(l->content, 0, l->made * (l->row_h + ROW_GAP), UI_WIDTH_SURFACE, l->row_h);
        r->icon = ui_icon(r->box, "", UI_C_FOG, PAD_H, (l->row_h - ICON_W) / 2);
        r->label = ui_text(r->box, UI_FONT_BODY, UI_C_CREAM, PAD_H + ICON_W + ICON_GAP,
                           (l->row_h - lv_font_get_line_height(UI_FONT_BODY)) / 2, 0, true);
        lv_label_set_long_mode(r->label, LV_LABEL_LONG_MODE_DOTS);
        r->value = ui_text(r->box, UI_FONT_SMALL, UI_C_FOG, 0,
                           (l->row_h - lv_font_get_line_height(UI_FONT_SMALL)) / 2, VALUE_W, true);
        lv_obj_set_style_text_align(r->value, LV_TEXT_ALIGN_RIGHT, 0);
    }
}

static void anim_y(void *var, int32_t v)
{
    lv_obj_set_y(var, v);
}

static void move_to(lv_obj_t *obj, int32_t y, uint32_t ms, bool animate)
{
    lv_anim_delete(obj, anim_y);
    if (!animate || lv_obj_get_y(obj) == y) {
        lv_obj_set_y(obj, y);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_y);
    lv_anim_set_values(&a, lv_obj_get_y(obj), y);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static lv_color_t focus_color(w_list_focus_t style)
{
    switch (style) {
    case W_LIST_FOCUS_AMBER:
        return UI_C_AMBER;
    case W_LIST_FOCUS_CORAL:
        return UI_C_CORAL;
    case W_LIST_FOCUS_CREAM:
        break;
    }
    return UI_C_CREAM;
}

static uint8_t desc_lines(const w_list_t *l, const w_list_row_t *row)
{
    if (row->desc == NULL || row->desc[0] == '\0') {
        return 0;
    }
    lv_point_t size;
    lv_text_get_size(&size, row->desc, UI_FONT_SMALL, 0, 0, l->width - 2 * PAD_H, LV_TEXT_FLAG_NONE);
    const int32_t n = (size.y + DESC_LINE_H - 1) / DESC_LINE_H;
    return (uint8_t)(n > DESC_LINES ? DESC_LINES : n < 1 ? 1 : n);
}

static uint32_t fnv1a(uint32_t h, const char *s)
{
    for (; s != NULL && *s != '\0'; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return (h ^ 0xFFu) * 16777619u;
}

/* What the list shows, so an unchanged call touches no object (an idle screen causes no redraw). */
static uint32_t rows_hash(const w_list_row_t *rows, uint8_t count, uint8_t focus, w_list_focus_t style)
{
    uint32_t h = 2166136261u ^ ((uint32_t)count << 16 | (uint32_t)focus << 8 | (uint32_t)style);
    for (uint8_t i = 0; i < count; i++) {
        h = fnv1a(fnv1a(fnv1a(fnv1a(h, rows[i].icon), rows[i].label), rows[i].value), rows[i].desc);
    }
    return h;
}

void w_list_set(w_list_t *l, const w_list_row_t *rows, uint8_t count, uint8_t focus, w_list_focus_t style)
{
    if (l == NULL) {
        return;
    }
    if (count > W_LIST_MAX_ROWS) {
        count = W_LIST_MAX_ROWS;
    }
    if (focus >= count) {
        focus = 0;
    }
    const uint32_t hash = rows_hash(rows, count, focus, style);
    if (l->valid && hash == l->hash) {
        return;
    }
    l->hash = hash;
    const bool moved = l->valid && focus != l->focused;
    make_rows(l, count);
    const int32_t full = count * (l->row_h + ROW_GAP) - ROW_GAP;
    /* A list that overflows gives the thumb its column. */
    l->width = full > l->h ? UI_WIDTH_SURFACE - THUMB_W - THUMB_GAP : UI_WIDTH_SURFACE;

    int32_t y = 0;
    int32_t focus_y = 0;
    int32_t focus_h = l->row_h;
    int32_t row_y[W_LIST_MAX_ROWS];
    uint8_t focus_lines = 0;
    for (uint8_t i = 0; i < l->made; i++) {
        row_obj_t *r = &l->rows[i];
        if (i >= count) {
            ui_set_hidden(r->box, true);
            continue;
        }
        const w_list_row_t *row = &rows[i];
        const bool on = i == focus;
        const uint8_t lines = on ? desc_lines(l, row) : 0;
        const int32_t h = l->row_h + (lines > 0 ? lines * DESC_LINE_H + DESC_PAD : 0);
        const bool has_icon = row->icon != NULL && row->icon[0] != '\0';
        if (strcmp(lv_label_get_text(r->icon), has_icon ? row->icon : "") != 0) {
            lv_label_set_text_static(r->icon, has_icon ? row->icon : "");
        }
        const int32_t text_x = has_icon ? PAD_H + ICON_W + ICON_GAP : PAD_H;
        const bool has_value = row->value != NULL && row->value[0] != '\0';
        lv_obj_set_x(r->label, text_x);
        lv_obj_set_width(r->label, l->width - text_x - PAD_H - (has_value ? VALUE_W / 2 : 0));
        ui_label_set(r->label, row->label);
        ui_set_hidden(r->value, !has_value);
        ui_label_set(r->value, has_value ? row->value : "");
        lv_obj_set_x(r->value, l->width - PAD_H - VALUE_W);
        if (on) {
            focus_lines = lines;
            ui_label_set(l->desc, lines > 0 ? row->desc : "");
        }
        lv_obj_set_style_text_color(r->icon, on ? UI_C_INK : UI_C_FOG, 0);
        lv_obj_set_style_text_color(r->label, on ? UI_C_INK : UI_C_CREAM, 0);
        lv_obj_set_style_text_color(r->value, on ? UI_C_SUB_ON_CREAM : UI_C_FOG, 0);
        lv_obj_set_size(r->box, l->width, h);
        lv_obj_set_y(r->box, y);
        ui_set_hidden(r->box, false);
        row_y[i] = y;
        if (on) {
            focus_y = y;
            focus_h = h;
        }
        y += h + ROW_GAP;
    }
    const int32_t total = y > 0 ? y - ROW_GAP : 0;
    ui_set_hidden(l->desc, focus_lines == 0);
    if (focus_lines > 0) {
        lv_obj_set_pos(l->desc, PAD_H, focus_y + l->row_h - DESC_PAD);
        lv_obj_set_size(l->desc, l->width - 2 * PAD_H, focus_lines * DESC_LINE_H);
    }

    /* Whole-row scrolling: the first visible row moves only as far as the focused row needs. */
    uint8_t top = l->valid && l->top < count ? l->top : 0;
    if (focus < top) {
        top = focus;
    }
    while (top < focus && focus_y + focus_h - row_y[top] > l->h) {
        top++;
    }
    const int32_t offset = count > 0 ? row_y[top] : 0;
    move_to(l->content, -offset, UI_MS_MOVE, moved);

    lv_obj_set_style_bg_color(l->focus, focus_color(style), 0);
    lv_obj_set_size(l->focus, l->width, focus_h);
    move_to(l->focus, focus_y, UI_MS_QUICK, moved);
    ui_set_hidden(l->focus, count == 0);

    const bool scroll = total > l->h;
    ui_set_hidden(l->track, !scroll);
    if (scroll) {
        lv_obj_set_height(l->thumb, l->h * l->h / total);
        lv_obj_set_y(l->thumb, l->h * offset / total);
    }
    l->count = count;
    l->focused = focus;
    l->top = top;
    l->style = style;
    l->valid = true;
}
