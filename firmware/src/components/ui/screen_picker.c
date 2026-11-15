#include "ui_priv.h"

#include "pet_builtin.h"
#include "ui.h"
#include "ui_theme.h"

#include <string.h>

/* Mockups 60-62: one pet per page at 12/16, its name, the In use tag, a line and page dots. */
#define PET_CX        160
#define PET_CY        96
#define PET_SCALE     12
#define NAME_Y        158
#define NAME_H        28
#define TAG_H         18
#define TAG_PAD       6
#define LINE_Y        186
#define DOTS_Y        205
#define DOT_D         6
#define DOT_PITCH     12
#define MORE_X        110
#define MORE_Y        30
#define MORE_W        100
#define MORE_H        84
#define MORE_R        30
#define MORE_TITLE_Y  122
#define MORE_TEXT_X   20
#define MORE_TEXT_Y   152
#define MORE_TEXT_W   280

#define MAX_DOTS      (PET_BUILTIN_COUNT + UI_PICKER_STORED_MAX + 1)

static pet_view_t *s_view;
static lv_obj_t *s_row;
static lv_obj_t *s_name;
static lv_obj_t *s_tag;
static lv_obj_t *s_line;
static lv_obj_t *s_more;
static lv_obj_t *s_dots[MAX_DOTS];

/* Guarded by the ui lock. */
static bool s_on_screen;
static bool s_more_page;
static char s_view_id[PET_PACK_ID_LEN + 1];
static uint8_t s_view_rev;
static char s_name_text[PET_PACK_NAME_LEN + 1];

static void apply_tabs(bool can_use)
{
    static const ui_tabs_t PAGE = {.tab = {
                                       [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                                       [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Next"},
                                   }};
    static const ui_tabs_t USE = {.tab = {
                                      [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                                      [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Next"},
                                      [UI_TAB_APPROVE] = {.state = UI_TAB_CLICK, .label = "Use"},
                                  }};
    ui_screen_set_tabs(UI_SCREEN_PICKER, can_use ? &USE : &PAGE);
}

lv_obj_t *screen_picker_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    ui_screen_set_title(UI_SCREEN_PICKER, "Pet");
    s_view = pet_view_create(scr, PET_CX, PET_CY, PET_SCALE);

    s_row = ui_box(scr, 0, NAME_Y, LV_HOR_RES, NAME_H);
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_row, UI_PAD, 0);
    s_name = lv_label_create(s_row);
    lv_obj_set_style_text_font(s_name, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_name, UI_C_CREAM, 0);
    lv_label_set_text_static(s_name, "");
    s_tag = lv_label_create(s_row);
    lv_obj_add_style(s_tag, &ui_style_caps, 0);
    lv_obj_set_style_text_color(s_tag, UI_C_INK, 0);
    lv_obj_set_style_bg_color(s_tag, UI_C_MINT, 0);
    lv_obj_set_style_bg_opa(s_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_tag, UI_R_TAG, 0);
    lv_obj_set_style_pad_hor(s_tag, TAG_PAD, 0);
    lv_obj_set_height(s_tag, TAG_H);
    lv_label_set_text_static(s_tag, "IN USE");

    s_line = ui_text(scr, UI_FONT_SMALL, UI_C_FOG, 0, LINE_Y, LV_HOR_RES, true);
    lv_obj_set_style_text_align(s_line, LV_TEXT_ALIGN_CENTER, 0);

    s_more = ui_box(scr, 0, 0, LV_HOR_RES, DOTS_Y);
    lv_obj_t *frame = ui_box(s_more, MORE_X, MORE_Y, MORE_W, MORE_H);
    lv_obj_set_style_border_color(frame, UI_C_LINE, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_radius(frame, MORE_R, 0);
    lv_obj_t *plus = lv_label_create(frame);
    lv_obj_set_style_text_font(plus, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(plus, UI_C_SLATE, 0);
    lv_label_set_text_static(plus, "+");
    lv_obj_center(plus);
    lv_obj_t *title = ui_text(s_more, UI_FONT_TITLE, UI_C_CREAM, 0, MORE_TITLE_Y, LV_HOR_RES, true);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(title, "More pets");
    lv_obj_t *text = ui_text(s_more, UI_FONT_SMALL, UI_C_FOG, MORE_TEXT_X, MORE_TEXT_Y, MORE_TEXT_W, false);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(text, "Pick up to eight more in the dashboard, under Pets. They are sent here.");
    lv_obj_add_flag(s_more, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < MAX_DOTS; i++) {
        s_dots[i] = ui_rect(scr, 0, DOTS_Y + 1, DOT_D, DOT_D, UI_C_LINE, DOT_D / 2);
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
    apply_tabs(false);
    return scr;
}

static void apply(const ui_picker_t *p)
{
    const bool more = p->def == NULL;
    ui_set_hidden(s_more, !more);
    ui_set_hidden(s_row, more);
    ui_set_hidden(s_line, more);
    if (!more) {
        /* The view keeps its own copy; it is rebuilt only when the page shows another pack. */
        if (strcmp(s_view_id, p->def->id) != 0 || s_view_rev != p->def->rev) {
            pet_view_set_def(s_view, p->def);
            pet_view_set_mood(s_view, MOOD_IDLE);
            strlcpy(s_view_id, p->def->id, sizeof(s_view_id));
            s_view_rev = p->def->rev;
        }
        strlcpy(s_name_text, p->def->name, sizeof(s_name_text));
        ui_label_set(s_name, s_name_text);
        ui_set_hidden(s_tag, !p->in_use);
        ui_label_set(s_line, p->line != NULL ? p->line : "");
    }
    s_more_page = more;
    pet_view_set_visible(s_view, !more && s_on_screen);
    const uint8_t pages = p->pages > MAX_DOTS ? MAX_DOTS : p->pages;
    const int32_t x0 = (LV_HOR_RES - pages * DOT_PITCH) / 2 + (DOT_PITCH - DOT_D) / 2;
    for (int i = 0; i < MAX_DOTS; i++) {
        ui_set_hidden(s_dots[i], i >= pages);
        if (i < pages) {
            lv_obj_set_x(s_dots[i], x0 + i * DOT_PITCH);
            lv_obj_set_style_bg_color(s_dots[i], i == p->page ? UI_C_CREAM : UI_C_LINE, 0);
        }
    }
    apply_tabs(p->can_use && !more);
}

void screen_picker_set_shown(bool shown)
{
    s_on_screen = shown;
    pet_view_set_visible(s_view, shown && !s_more_page);
}

void ui_picker_set(const ui_picker_t *p)
{
    if (p == NULL || !ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(p);
    }
    ui_unlock();
}

void screen_picker_demo(int variant)
{
    static pet_def_t def;
    /* The showcase reproduces the design renders of Devo and Ember, whatever pets/builtin.txt lists. */
    const pet_builtin_t *b = pet_builtin_find(variant == 1 ? "ember" : "devo");
    ui_picker_t p = {.pages = 3, .page = (uint8_t)variant};
    if (variant < 2 && b != NULL && pet_pack_decode(b->bytes, b->len, &def) == PET_PACK_OK) {
        p.def = &def;
        p.in_use = variant == 0;
        p.can_use = variant != 0;
        p.line = variant == 0 ? "Built in \xC2\xB7 changes colour with its mood" : "Built in \xC2\xB7 a small jade dragon";
    }
    apply(&p);
}
