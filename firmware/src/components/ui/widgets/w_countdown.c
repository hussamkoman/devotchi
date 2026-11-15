#include "ui_priv.h"

#include "ui_theme.h"

#include <stdio.h>

#define BAR_H      3
#define SECONDS_W  68
#define SECONDS_DY 23

struct w_countdown {
    lv_obj_t *track;
    lv_obj_t *fill;
    lv_obj_t *seconds;
    int32_t width;
    int32_t shown_s;
    lv_color_t color;
    bool valid;
};

/* One per screen that shows time left; screens are created once, so is this. */
#define W_COUNTDOWN_MAX  6
static struct w_countdown s_pool[W_COUNTDOWN_MAX];
static size_t s_used;

w_countdown_t *w_countdown_create(lv_obj_t *parent, int32_t y, bool with_seconds)
{
    if (s_used >= W_COUNTDOWN_MAX) {
        return NULL;
    }
    w_countdown_t *c = &s_pool[s_used++];
    c->track = ui_rect(parent, UI_PAD, y, UI_WIDTH_SURFACE, BAR_H, UI_C_SURFACE2, 2);
    c->fill = ui_rect(c->track, 0, 0, 0, BAR_H, UI_C_CREAM, 2);
    if (with_seconds) {
        c->seconds = ui_text(parent, UI_FONT_SMALL, UI_C_FOG, LV_HOR_RES - UI_TEXT_X - SECONDS_W, y - SECONDS_DY,
                             SECONDS_W, true);
        lv_obj_set_style_text_align(c->seconds, LV_TEXT_ALIGN_RIGHT, 0);
    }
    c->valid = false;
    return c;
}

void w_countdown_set(w_countdown_t *c, uint16_t permille, int32_t seconds, lv_color_t color)
{
    if (c == NULL) {
        return;
    }
    const int32_t width = (int32_t)UI_WIDTH_SURFACE * (permille > 1000 ? 1000 : permille) / 1000;
    if (!c->valid || width != c->width) {
        lv_obj_set_width(c->fill, width);
        c->width = width;
    }
    if (!c->valid || !lv_color_eq(color, c->color)) {
        lv_obj_set_style_bg_color(c->fill, color, 0);
        c->color = color;
    }
    if (c->seconds != NULL && (!c->valid || seconds != c->shown_s)) {
        char text[16];
        snprintf(text, sizeof(text), "%ld s", (long)(seconds < 0 ? 0 : seconds));
        ui_label_set(c->seconds, text);
        c->shown_s = seconds;
    }
    c->valid = true;
}
