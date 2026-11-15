#include "ui_priv.h"

#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <ctype.h>
#include <string.h>

/* Layout of the mockups: lists (50, 51, 16b, 80), value editor (52), destructive confirm (53). */
#define LIST_Y        24
#define LIST_H        186
#define ROW_H         29
#define NOTE_GAP      10
#define BIG_Y         34
#define SEG_Y         132
#define SEG_H         8
#define SEG_GAP       6
#define CHOICES_Y     148
#define HINT_Y        180
#define WARN_Y        34
#define WARN_D        48
#define CONFIRM_TITLE_Y 92
#define CONFIRM_TEXT_Y  126
#define MAX_STEPS     8

static w_list_t *s_list;
static lv_obj_t *s_note;
static lv_obj_t *s_editor;
static lv_obj_t *s_big_row;
static lv_obj_t *s_big;
static lv_obj_t *s_unit;
static lv_obj_t *s_segs[MAX_STEPS];
static lv_obj_t *s_choices;
static lv_obj_t *s_hint;
static lv_obj_t *s_confirm;
static lv_obj_t *s_confirm_title;
static lv_obj_t *s_confirm_text;
/* The tab texts must outlive the call (ui_screen_set_tabs keeps the pointers). */
static char s_approve[UI_SETTINGS_VALUE_SIZE];
static char s_hold[UI_SETTINGS_VALUE_SIZE];
static char s_holding[UI_SETTINGS_VALUE_SIZE + 4];
static char s_title[32];

lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_list = w_list_create(scr, LIST_Y, LIST_H, ROW_H);
    s_note = ui_text(scr, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, 0, UI_WIDTH_TEXT, false);
    lv_obj_add_flag(s_note, LV_OBJ_FLAG_HIDDEN);

    s_editor = ui_box(scr, 0, 0, LV_HOR_RES, UI_TABS_Y);
    s_big_row = ui_box(s_editor, 0, BIG_Y, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_big_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_big_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(s_big_row, 4, 0);
    s_big = ui_text(s_big_row, UI_FONT_DISPLAY, UI_C_CREAM, 0, 0, 0, true);
    s_unit = ui_text(s_big_row, UI_FONT_TITLE, UI_C_CREAM, 0, 0, 0, true);
    lv_obj_set_style_pad_bottom(s_unit, 10, 0);
    for (int i = 0; i < MAX_STEPS; i++) {
        s_segs[i] = ui_rect(s_editor, 0, SEG_Y, 10, SEG_H, UI_C_SURFACE2, SEG_H / 2);
    }
    s_choices = ui_text_line(s_editor, UI_FONT_SMALL, UI_C_FOG, CHOICES_Y, LV_TEXT_ALIGN_CENTER);
    s_hint = ui_text_line(s_editor, UI_FONT_SMALL, UI_C_FOG, HINT_Y, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);

    s_confirm = ui_box(scr, 0, 0, LV_HOR_RES, UI_TABS_Y);
    lv_obj_t *warn = ui_rect(s_confirm, (LV_HOR_RES - WARN_D) / 2, WARN_Y, WARN_D, WARN_D, UI_C_CORAL,
                             LV_RADIUS_CIRCLE);
    lv_obj_t *icon = ui_icon(warn, UI_ICON_WARN, UI_C_INK, 0, 0);
    lv_obj_center(icon);
    s_confirm_title = ui_text_line(s_confirm, UI_FONT_TITLE, UI_C_CREAM, CONFIRM_TITLE_Y, LV_TEXT_ALIGN_CENTER);
    s_confirm_text = ui_text(s_confirm, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, CONFIRM_TEXT_Y, UI_WIDTH_TEXT, false);
    lv_obj_set_style_text_align(s_confirm_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    return scr;
}

static bool all_digits(const char *s)
{
    if (s[0] == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

static void apply_list(const ui_settings_t *s)
{
    w_list_row_t rows[UI_SETTINGS_ROWS];
    const uint8_t count = s->count <= UI_SETTINGS_ROWS ? s->count : UI_SETTINGS_ROWS;
    const uint8_t cursor = s->cursor < count ? s->cursor : 0;
    for (uint8_t i = 0; i < count; i++) {
        rows[i] = (w_list_row_t){
            .icon = s->icons[i],
            .label = s->labels[i],
            .value = s->values[i],
            .chevron = s->chevron[i],
            .muted = s->muted[i],
            .desc = i == cursor ? s->desc : NULL,
        };
    }
    /* A note (quick menu) sits under the rows, so the list is only as tall as they are. */
    const bool note = s->note != NULL && s->note[0] != '\0';
    const int32_t rows_h = count * (ROW_H + 2);
    w_list_move(s_list, LIST_Y, note ? rows_h : LIST_H);
    w_list_set(s_list, rows, count, cursor, s->muted[cursor] ? W_LIST_FOCUS_MUTED : W_LIST_FOCUS_CREAM);
    ui_set_hidden(s_note, !note);
    if (note) {
        lv_obj_set_y(s_note, LIST_Y + rows_h + NOTE_GAP);
        ui_label_set(s_note, s->note);
    }
    strlcpy(s_approve, s->approve != NULL ? s->approve : "Open", sizeof(s_approve));
    const ui_tab_t back = s->reject_hold != NULL
                              ? (ui_tab_t){.state = UI_TAB_CLICK_AND_HOLD, .label = "Up", .hold = s->reject_hold}
                              : (ui_tab_t){.state = UI_TAB_CLICK, .label = "Back"};
    const ui_tabs_t tabs = {.tab = {
                               [UI_TAB_REJECT] = back,
                               [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Next"},
                               [UI_TAB_APPROVE] = {.state = s->muted[cursor] ? UI_TAB_INACTIVE : UI_TAB_CLICK,
                                                   .label = s_approve},
                           }};
    ui_screen_set_tabs(UI_SCREEN_SETTINGS, &tabs);
}

static void apply_editor(const ui_settings_t *s)
{
    lv_obj_set_style_text_font(s_big, all_digits(s->big) ? UI_FONT_DISPLAY : UI_FONT_TITLE, 0);
    ui_label_set(s_big, s->big);
    ui_label_set(s_unit, s->unit);
    ui_set_hidden(s_unit, s->unit[0] == '\0');
    const uint8_t steps = s->steps <= MAX_STEPS ? s->steps : MAX_STEPS;
    const int32_t w = steps > 0 ? (UI_WIDTH_SURFACE - (steps - 1) * SEG_GAP) / steps : 0;
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
        ui_set_hidden(s_segs[i], i >= steps);
        if (i < steps) {
            lv_obj_set_pos(s_segs[i], UI_PAD + i * (w + SEG_GAP), SEG_Y);
            lv_obj_set_width(s_segs[i], w);
            lv_obj_set_style_bg_color(s_segs[i], i <= s->step ? UI_C_CREAM : UI_C_SURFACE2, 0);
        }
    }
    ui_label_set(s_choices, s->choices);
    ui_label_set(s_hint, s->hint != NULL ? s->hint : "");
    static const ui_tabs_t TABS = {.tab = {
                                       [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Cancel"},
                                       [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Step"},
                                       [UI_TAB_APPROVE] = {.state = UI_TAB_CLICK, .label = "Save"},
                                   }};
    ui_screen_set_tabs(UI_SCREEN_SETTINGS, &TABS);
}

static void apply_confirm(const ui_settings_t *s)
{
    ui_label_set(s_confirm_title, s->confirm_title != NULL ? s->confirm_title : "");
    ui_label_set(s_confirm_text, s->confirm_text != NULL ? s->confirm_text : "");
    strlcpy(s_hold, s->confirm_hold != NULL ? s->confirm_hold : "Erase", sizeof(s_hold));
    snprintf(s_holding, sizeof(s_holding), "%s\xE2\x80\xA6", s_hold);
    const ui_tabs_t tabs = {.tab = {
                               [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Cancel"},
                               [UI_TAB_APPROVE] = {.state = UI_TAB_HOLD_ONLY, .hold = s_hold, .holding = s_holding,
                                                   .destructive = true},
                           }};
    ui_screen_set_tabs(UI_SCREEN_SETTINGS, &tabs);
}

static void apply(const ui_settings_t *s)
{
    ui_set_hidden(w_list_obj(s_list), s->view != UI_SETTINGS_LIST);
    ui_set_hidden(s_note, true);
    ui_set_hidden(s_editor, s->view != UI_SETTINGS_EDITOR);
    ui_set_hidden(s_confirm, s->view != UI_SETTINGS_CONFIRM);
    strlcpy(s_title, s->title != NULL ? s->title : "", sizeof(s_title));
    ui_screen_set_title(UI_SCREEN_SETTINGS, s_title);
    switch (s->view) {
    case UI_SETTINGS_EDITOR:
        apply_editor(s);
        break;
    case UI_SETTINGS_CONFIRM:
        apply_confirm(s);
        break;
    case UI_SETTINGS_LIST:
        apply_list(s);
        break;
    }
}

void ui_settings_set(const ui_settings_t *s)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(s);
    }
    ui_unlock();
}

static void demo_rows(ui_settings_t *s, const char *const *icons, const char *const *labels,
                      const char *const *values, uint8_t count)
{
    s->count = count;
    for (uint8_t i = 0; i < count; i++) {
        s->icons[i] = icons[i];
        s->labels[i] = labels[i];
        strlcpy(s->values[i], values[i] != NULL ? values[i] : "", sizeof(s->values[i]));
        s->chevron[i] = values[i] == NULL;
    }
}

void screen_settings_demo(int variant)
{
    static ui_settings_t s;
    memset(&s, 0, sizeof(s));
    s.approve = "Open";
    switch (variant) {
    case 1: {
        static const char *const ICONS[] = {UI_ICON_SUN, UI_ICON_CLOCK, UI_ICON_MOON};
        static const char *const LABELS[] = {"Brightness", "Screen timeout", "Deep sleep"};
        static const char *const VALUES[] = {"75 %", "30 s", "20 min"};
        s.title = "Display";
        s.approve = "Change";
        demo_rows(&s, ICONS, LABELS, VALUES, 3);
        break;
    }
    case 2:
        s.view = UI_SETTINGS_EDITOR;
        s.title = "Brightness";
        strlcpy(s.big, "75", sizeof(s.big));
        strlcpy(s.unit, "%", sizeof(s.unit));
        s.steps = 4;
        s.step = 2;
        strlcpy(s.choices, "25 \xC2\xB7 50 \xC2\xB7 75 \xC2\xB7 100", sizeof(s.choices));
        s.hint = "The screen shows each value as you step.";
        break;
    case 3:
        s.view = UI_SETTINGS_CONFIRM;
        s.title = "Network";
        s.confirm_title = "Factory reset?";
        s.confirm_text = "Erases Wi-Fi, pairing and settings, then restarts into setup.";
        s.confirm_hold = "Erase";
        break;
    case 4: {
        static const char *const ICONS[] = {UI_ICON_MENU, UI_ICON_PLAY, UI_ICON_DESK};
        static const char *const LABELS[] = {"Settings", "Showcase", "Desk mode"};
        static const char *const VALUES[] = {NULL, NULL, "Off"};
        s.title = "Menu";
        s.note = "Desk mode sends every request straight to your terminal. Showcase is a demo for visitors.";
        demo_rows(&s, ICONS, LABELS, VALUES, 3);
        break;
    }
    case 5: {
        static const char *const ICONS[] = {UI_ICON_PET, UI_ICON_PLAY, UI_ICON_SHIELD, UI_ICON_Q,
                                            UI_ICON_BELL, UI_ICON_CLOCK, UI_ICON_X};
        static const char *const LABELS[] = {"Moods", "Reactions", "Sample permission", "Sample question",
                                             "Sample notification", "Clock face", "Leave showcase"};
        static const char *const VALUES[] = {"11", "5", "", "", "", "", ""};
        s.title = "Showcase";
        demo_rows(&s, ICONS, LABELS, VALUES, 7);
        for (int i = 0; i < 7; i++) {
            s.chevron[i] = false;
        }
        break;
    }
    default: {
        static const char *const ICONS[] = {UI_ICON_PET,  UI_ICON_SUN,  UI_ICON_CLOCK, UI_ICON_BUZZ,
                                            UI_ICON_DESK, UI_ICON_WIFI, UI_ICON_PLAY,  UI_ICON_INFO};
        static const char *const LABELS[] = {"Pet",       "Display", "Clock",    "Haptics and LED",
                                             "Desk mode", "Network", "Showcase", "About"};
        static const char *const VALUES[] = {"Devo", NULL, NULL, NULL, "Off", NULL, NULL, NULL};
        s.title = "Settings";
        s.cursor = 1;
        demo_rows(&s, ICONS, LABELS, VALUES, 8);
        break;
    }
    }
    apply(&s);
}
