#include "ui_priv.h"

#include "text.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Button centres seen from the front (design-system 5) and the edge the tabs keep. */
static const int32_t CENTRE_X[UI_TAB_COUNT] = {50, 160, 270};
#define EDGE          4
#define GAP           3
#define PAD_X         6
#define HOLD_W        136
#define HOLD_H        26
#define FILL_H        4
#define FILL_INSET    8
#define SHAKE_PX      4
#define NUDGE_MS      1500
#define HOLD_WORD_PAD 5

typedef struct {
    lv_obj_t *box;
    lv_obj_t *glyph;
    lv_obj_t *text;
    lv_obj_t *hold_word;
    lv_obj_t *hold_text;
    lv_obj_t *fill;
} tab_obj_t;

typedef struct {
    ui_tab_state_t state;
    char label[UI_TAB_TEXT_SIZE];
    char hold[UI_TAB_TEXT_SIZE];
    char holding[UI_TAB_TEXT_SIZE];
    bool destructive;
} tab_copy_t;

/* Guarded by the ui lock. */
static tab_obj_t s_tab[UI_TAB_COUNT];
static tab_copy_t s_set[UI_TAB_COUNT];
static char s_any[UI_TAB_TEXT_SIZE];
static bool s_valid;
static bool s_shown;
static int s_hold_btn = -1;
static uint16_t s_hold_permille;
static int s_nudge_btn = -1;
static char s_nudge[UI_TAB_TEXT_SIZE];
static lv_timer_t *s_nudge_timer;

static const char *const BASE_GLYPH[UI_TAB_COUNT] = {UI_ICON_X, UI_ICON_DOT, UI_ICON_CHECK};
static const char *const HOLD_GLYPH[UI_TAB_COUNT] = {UI_ICON_HOLD_X, UI_ICON_HOLD_DOT, UI_ICON_HOLD_CHECK};

const char *ui_tab_state_name(ui_tab_state_t s)
{
    switch (s) {
    case UI_TAB_NONE:
        return "none";
    case UI_TAB_CLICK:
        return "click";
    case UI_TAB_HOLD_ONLY:
        return "hold_only";
    case UI_TAB_CLICK_AND_HOLD:
        return "click_and_hold";
    case UI_TAB_INACTIVE:
        return "inactive";
    }
    return "none";
}

const char *ui_tab_btn_name(ui_tab_btn_t b)
{
    switch (b) {
    case UI_TAB_REJECT:
        return "reject";
    case UI_TAB_ACTION:
        return "action";
    case UI_TAB_APPROVE:
        return "approve";
    case UI_TAB_COUNT:
        break;
    }
    return "";
}

static lv_color_t glyph_color(int i, const tab_copy_t *t)
{
    if (i == UI_TAB_REJECT) {
        return UI_C_CORAL;
    }
    if (i == UI_TAB_APPROVE) {
        return t->destructive ? UI_C_CORAL : UI_C_MINT;
    }
    return UI_C_CREAM;
}

static lv_obj_t *tab_label(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text_static(l, "");
    return l;
}

static void nudge_expired(lv_timer_t *t);

void w_tabs_create(void)
{
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        tab_obj_t *o = &s_tab[i];
        /* The body reaches UI_R_TAB below the screen edge, so only its top corners show rounded. */
        o->box = ui_box(lv_layer_top(), 0, UI_TABS_Y, LV_SIZE_CONTENT, UI_TAB_H + UI_R_TAB);
        lv_obj_add_style(o->box, &ui_style_tab, 0);
        lv_obj_set_style_pad_hor(o->box, PAD_X, 0);
        lv_obj_set_style_pad_bottom(o->box, UI_R_TAB, 0);
        lv_obj_set_style_pad_column(o->box, 4, 0);
        lv_obj_set_flex_flow(o->box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(o->box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        /* Reported by ui_tabs_get(), not in the screen's texts. */
        lv_obj_add_flag(o->box, UI_FLAG_NO_TEXT);
        o->glyph = lv_label_create(o->box);
        lv_obj_set_style_text_font(o->glyph, UI_FONT_ICONS, 0);
        lv_label_set_text_static(o->glyph, BASE_GLYPH[i]);
        o->text = tab_label(o->box, UI_C_CREAM);
        o->hold_word = tab_label(o->box, UI_C_FOG);
        lv_obj_set_style_pad_left(o->hold_word, HOLD_WORD_PAD, 0);
        lv_label_set_text_static(o->hold_word, "hold");
        o->hold_text = tab_label(o->box, UI_C_CREAM);
        o->fill = ui_rect(o->box, FILL_INSET, 0, 0, FILL_H, UI_C_MINT, 2);
        lv_obj_add_flag(o->fill, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(o->box, LV_OBJ_FLAG_HIDDEN);
    }
    s_nudge_timer = lv_timer_create(nudge_expired, NUDGE_MS, NULL);
    lv_timer_pause(s_nudge_timer);
}

static void anim_translate_y(void *var, int32_t v)
{
    lv_obj_set_style_translate_y(var, v, 0);
}

static void anim_shake(void *var, int32_t v)
{
    /* Two swings either way that die down: v runs 0..1000. */
    const int32_t sign = (v / 250) % 2 == 0 ? 1 : -1;
    lv_obj_set_style_translate_x(var, sign * SHAKE_PX * (1000 - v) / 1000, 0);
}

static void rise(lv_obj_t *box)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_exec_cb(&a, anim_translate_y);
    lv_anim_set_values(&a, UI_TAB_H, 0);
    lv_anim_set_duration(&a, UI_MS_MOVE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void shake(lv_obj_t *box)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_exec_cb(&a, anim_shake);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, UI_MS_QUICK);
    lv_anim_start(&a);
}

/* What the tab says, glyph aside; also the console's view of it. */
static void tab_text(int i, char out[UI_TAB_TEXT_SIZE])
{
    const tab_copy_t *t = &s_set[i];
    out[0] = '\0';
    if (s_any[0] != '\0') {
        if (i == UI_TAB_ACTION) {
            strlcpy(out, s_any, UI_TAB_TEXT_SIZE);
        }
        return;
    }
    if (s_hold_btn >= 0) {
        if (i == s_hold_btn) {
            strlcpy(out, t->holding[0] != '\0' ? t->holding : t->hold, UI_TAB_TEXT_SIZE);
        }
        return;
    }
    if (i == s_nudge_btn) {
        strlcpy(out, s_nudge, UI_TAB_TEXT_SIZE);
        return;
    }
    switch (t->state) {
    case UI_TAB_NONE:
        break;
    case UI_TAB_CLICK:
    case UI_TAB_INACTIVE:
        strlcpy(out, t->label, UI_TAB_TEXT_SIZE);
        break;
    case UI_TAB_HOLD_ONLY:
        snprintf(out, UI_TAB_TEXT_SIZE, "Hold: %s", t->hold);
        break;
    case UI_TAB_CLICK_AND_HOLD:
        snprintf(out, UI_TAB_TEXT_SIZE, "%s hold %s", t->label, t->hold);
        break;
    }
}

/* Fills tab i's objects; returns false when it has no tab. */
static bool draw_tab(int i)
{
    tab_obj_t *o = &s_tab[i];
    const tab_copy_t *t = &s_set[i];
    const lv_color_t color = glyph_color(i, t);
    const char *glyph = BASE_GLYPH[i];
    lv_color_t glyph_c = color;
    lv_color_t text_c = UI_C_CREAM;
    char text[UI_TAB_TEXT_SIZE];
    bool hold_part = false;
    bool holding = false;

    if (s_any[0] != '\0') {
        if (i != UI_TAB_ACTION) {
            return false;
        }
        strlcpy(text, s_any, sizeof(text));
    } else if (s_hold_btn >= 0) {
        if (i != s_hold_btn) {
            return false;
        }
        holding = true;
        glyph = HOLD_GLYPH[i];
        tab_text(i, text);
    } else if (i == s_nudge_btn) {
        glyph = HOLD_GLYPH[i];
        glyph_c = UI_C_AMBER;
        strlcpy(text, s_nudge, sizeof(text));
    } else {
        switch (t->state) {
        case UI_TAB_NONE:
            return false;
        case UI_TAB_INACTIVE:
            glyph_c = UI_C_SLATE;
            text_c = UI_C_SLATE;
            strlcpy(text, t->label, sizeof(text));
            break;
        case UI_TAB_CLICK:
            strlcpy(text, t->label, sizeof(text));
            break;
        case UI_TAB_HOLD_ONLY:
            glyph = HOLD_GLYPH[i];
            tab_text(i, text);
            break;
        case UI_TAB_CLICK_AND_HOLD:
            strlcpy(text, t->label, sizeof(text));
            hold_part = true;
            break;
        }
    }
    lv_label_set_text_static(o->glyph, glyph);
    lv_obj_set_style_text_color(o->glyph, glyph_c, 0);
    lv_obj_set_style_text_color(o->text, text_c, 0);
    ui_label_set(o->text, text);
    ui_set_hidden(o->hold_word, !hold_part);
    ui_set_hidden(o->hold_text, !hold_part);
    if (hold_part) {
        ui_label_set(o->hold_text, t->hold);
    }
    ui_set_hidden(o->fill, !holding);
    if (holding) {
        lv_obj_set_size(o->box, HOLD_W, HOLD_H + UI_R_TAB);
        lv_obj_set_y(o->box, LV_VER_RES - HOLD_H);
        lv_obj_set_style_bg_color(o->fill, color, 0);
        lv_obj_set_width(o->fill, (int32_t)(HOLD_W - 2 * FILL_INSET) * s_hold_permille / 1000);
    } else {
        lv_obj_set_size(o->box, LV_SIZE_CONTENT, UI_TAB_H + UI_R_TAB);
        lv_obj_set_y(o->box, UI_TABS_Y);
    }
    return true;
}

static int32_t min32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

/*
 * Centres every tab on its button; long side tabs move towards the screen
 * edge and the middle one gives way, but each still covers its button's x.
 */
static void place(const bool vis[UI_TAB_COUNT])
{
    lv_obj_update_layout(lv_layer_top());
    int32_t w[UI_TAB_COUNT];
    int32_t x[UI_TAB_COUNT];
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        w[i] = vis[i] ? lv_obj_get_width(s_tab[i].box) : 0;
        x[i] = min32(LV_HOR_RES - EDGE - w[i], max32(EDGE, CENTRE_X[i] - w[i] / 2));
    }
    if (vis[UI_TAB_ACTION] && s_hold_btn < 0) {
        const int32_t wm = w[UI_TAB_ACTION];
        int32_t m = CENTRE_X[UI_TAB_ACTION] - wm / 2;
        if (vis[UI_TAB_REJECT]) {
            x[UI_TAB_REJECT] = max32(EDGE, min32(x[UI_TAB_REJECT], m - GAP - w[UI_TAB_REJECT]));
        }
        if (vis[UI_TAB_APPROVE]) {
            x[UI_TAB_APPROVE] = min32(LV_HOR_RES - EDGE - w[UI_TAB_APPROVE], max32(x[UI_TAB_APPROVE], m + wm + GAP));
            if (m + wm + GAP > x[UI_TAB_APPROVE]) {
                m = x[UI_TAB_APPROVE] - GAP - wm;
            }
        }
        if (vis[UI_TAB_REJECT] && m < x[UI_TAB_REJECT] + w[UI_TAB_REJECT] + GAP) {
            m = x[UI_TAB_REJECT] + w[UI_TAB_REJECT] + GAP;
        }
        x[UI_TAB_ACTION] = m;
    }
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (vis[i]) {
            lv_obj_set_x(s_tab[i].box, x[i]);
        }
    }
}

/* Redraws every tab from s_set and the hold and nudge overrides; rising makes newly shown tabs rise. */
static void redraw(bool rising)
{
    bool vis[UI_TAB_COUNT];
    bool any = false;
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        vis[i] = draw_tab(i);
        any = any || vis[i];
    }
    place(vis);
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        lv_obj_t *box = s_tab[i].box;
        const bool was_hidden = lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN);
        ui_set_hidden(box, !vis[i]);
        if (vis[i] && was_hidden && rising) {
            rise(box);
        }
    }
    s_shown = any;
}

static void copy_str(char dst[UI_TAB_TEXT_SIZE], const char *src)
{
    text_utf8_truncate(dst, UI_TAB_TEXT_SIZE, src != NULL ? src : "");
}

void w_tabs_set(const ui_tabs_t *tabs)
{
    tab_copy_t next[UI_TAB_COUNT];
    char any[UI_TAB_TEXT_SIZE];
    memset(next, 0, sizeof(next));
    copy_str(any, tabs != NULL ? tabs->any : NULL);
    for (int i = 0; i < UI_TAB_COUNT && tabs != NULL; i++) {
        next[i].state = tabs->tab[i].state;
        copy_str(next[i].label, tabs->tab[i].label);
        copy_str(next[i].hold, tabs->tab[i].hold);
        copy_str(next[i].holding, tabs->tab[i].holding);
        next[i].destructive = tabs->tab[i].destructive;
    }
    if (s_valid && memcmp(next, s_set, sizeof(next)) == 0 && strcmp(any, s_any) == 0) {
        return;
    }
    memcpy(s_set, next, sizeof(next));
    strlcpy(s_any, any, sizeof(s_any));
    s_valid = true;
    s_hold_btn = -1;
    s_nudge_btn = -1;
    lv_timer_pause(s_nudge_timer);
    /* Content that changes under visible tabs is swapped in place; tabs rise only when none were showing. */
    const bool rising = !s_shown;
    for (int i = 0; i < UI_TAB_COUNT && rising; i++) {
        lv_obj_add_flag(s_tab[i].box, LV_OBJ_FLAG_HIDDEN);
    }
    redraw(rising);
}

static void nudge_expired(lv_timer_t *t)
{
    lv_timer_pause(t);
    s_nudge_btn = -1;
    redraw(false);
}

void ui_tabs_hold_progress(ui_tab_btn_t btn, uint16_t permille)
{
    if (btn >= UI_TAB_COUNT || !ui_lock()) {
        return;
    }
    const int held = permille > 0 ? (int)btn : -1;
    s_hold_permille = permille > 1000 ? 1000 : permille;
    if (held != s_hold_btn) {
        s_hold_btn = held;
        redraw(false);
    } else if (held >= 0) {
        lv_obj_set_width(s_tab[held].fill, (int32_t)(HOLD_W - 2 * FILL_INSET) * s_hold_permille / 1000);
    }
    ui_unlock();
}

void ui_tabs_nudge(ui_tab_btn_t btn, const char *label)
{
    if (btn >= UI_TAB_COUNT || !ui_lock()) {
        return;
    }
    s_nudge_btn = (int)btn;
    copy_str(s_nudge, label);
    redraw(false);
    shake(s_tab[btn].box);
    lv_timer_reset(s_nudge_timer);
    lv_timer_resume(s_nudge_timer);
    ui_unlock();
}

bool ui_tabs_get(ui_tab_info_t out[UI_TAB_COUNT])
{
    memset(out, 0, sizeof(ui_tab_info_t) * UI_TAB_COUNT);
    if (!ui_lock()) {
        return false;
    }
    const bool any = s_any[0] != '\0';
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (lv_obj_has_flag(s_tab[i].box, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        out[i].state = any ? UI_TAB_CLICK : s_set[i].state;
        if (i == s_nudge_btn || i == s_hold_btn) {
            out[i].state = UI_TAB_HOLD_ONLY;
        }
        tab_text(i, out[i].text);
    }
    ui_unlock();
    return any;
}
