#include "ui_priv.h"

#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Layout of the mockups: question (30-35), read page (33), question menu (36), review (37). */
#define HEAD_Y        25
#define HEAD_H        22
#define HEAD_GAP      8
#define TEXT_Y        48
#define LIST_Y        94
#define LIST_Y_SHORT  72
#define LIST_BOTTOM   206
#define ROW_H         25
#define MENU_Y        50
#define MENU_H        152
#define NOTE_Y        186
#define REVIEW_Y      50
#define REVIEW_GAP    6
#define WELL_H        56
#define WELL_H_SHORT  30
#define WELL_PAD      10
#define WELL_LABEL_W  96
#define BAR_Y         208
#define SENT_Y        220

static lv_obj_t *s_head_caps;
static lv_obj_t *s_head_text;
static lv_obj_t *s_head_right;

static lv_obj_t *s_card;
static lv_obj_t *s_text;
static w_list_t *s_options;

static lv_obj_t *s_read;
static lv_obj_t *s_read_text;
static lv_obj_t *s_read_note;

static lv_obj_t *s_menu;
static w_list_t *s_menu_list;

static lv_obj_t *s_review;
static lv_obj_t *s_wells[PROTO_Q_MAX];
static lv_obj_t *s_well_label[PROTO_Q_MAX];
static lv_obj_t *s_well_answer[PROTO_Q_MAX];
static lv_obj_t *s_review_note;

static w_countdown_t *s_bar;
static lv_obj_t *s_sent;

/* Guarded by the ui lock: the geometry last applied, so an unchanged frame touches nothing. */
static int8_t s_text_lines = -1;
static int8_t s_review_short = -1;

lv_obj_t *screen_question_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    const int32_t line_h = lv_font_get_line_height(UI_FONT_BODY);

    lv_obj_t *head = ui_box(scr, UI_PAD, HEAD_Y, UI_WIDTH_SURFACE, HEAD_H);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, HEAD_GAP, 0);
    w_chip_t *chip = w_chip_create(head, 0, 0);
    w_chip_set(chip, UI_ICON_Q, "Question", UI_C_SKY, 0, NULL);
    s_head_caps = ui_text(head, UI_FONT_SMALL, UI_C_SKY, 0, 0, 0, true);
    lv_obj_add_style(s_head_caps, &ui_style_caps, 0);
    lv_obj_set_style_text_color(s_head_caps, UI_C_SKY, 0);
    lv_label_set_long_mode(s_head_caps, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_head_caps, 1);
    s_head_text = ui_text(head, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);
    lv_label_set_long_mode(s_head_text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_head_text, 1);
    s_head_right = ui_text(head, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);

    s_card = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    s_text = ui_text(s_card, UI_FONT_BODY, UI_C_CREAM, UI_TEXT_X, TEXT_Y, UI_WIDTH_TEXT, false);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_MODE_DOTS);
    s_options = w_list_create(s_card, LIST_Y, LIST_BOTTOM - LIST_Y, ROW_H);
    w_list_set_label_lines(s_options, 2);

    s_read = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    lv_obj_t *clip = ui_box(s_read, UI_TEXT_X, TEXT_Y, UI_WIDTH_TEXT, CARD_READ_LINES * line_h);
    s_read_text = ui_text(clip, UI_FONT_BODY, UI_C_CREAM, 0, 0, UI_WIDTH_TEXT, false);
    s_read_note = ui_text_line(s_read, UI_FONT_SMALL, UI_C_FOG, NOTE_Y, LV_TEXT_ALIGN_LEFT);

    s_menu = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    s_menu_list = w_list_create(s_menu, MENU_Y, MENU_H, ROW_H);

    s_review = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    for (int i = 0; i < PROTO_Q_MAX; i++) {
        s_wells[i] = ui_box(s_review, UI_PAD, REVIEW_Y, UI_WIDTH_SURFACE, WELL_H);
        lv_obj_add_style(s_wells[i], &ui_style_well, 0);
        s_well_label[i] = ui_text(s_wells[i], UI_FONT_SMALL, UI_C_FOG, WELL_PAD, 0, 0, true);
        lv_obj_add_style(s_well_label[i], &ui_style_caps, 0);
        lv_label_set_long_mode(s_well_label[i], LV_LABEL_LONG_MODE_DOTS);
        s_well_answer[i] = ui_text(s_wells[i], UI_FONT_BODY, UI_C_CREAM, WELL_PAD, 0, 0, true);
        lv_label_set_long_mode(s_well_answer[i], LV_LABEL_LONG_MODE_DOTS);
    }
    s_review_note = ui_text_line(s_review, UI_FONT_SMALL, UI_C_FOG, NOTE_Y, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text_static(s_review_note, "Answers cannot be changed once sent.");

    s_bar = w_countdown_create(scr, BAR_Y, false);
    s_sent = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, SENT_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(s_sent, "Sent\xE2\x80\xA6");
    lv_obj_add_flag(s_sent, LV_OBJ_FLAG_HIDDEN);
    return scr;
}

uint16_t ui_question_lines(const char *text)
{
    if (text == NULL || text[0] == '\0' || !ui_lock()) {
        return 0;
    }
    lv_point_t size;
    lv_text_get_size(&size, text, UI_FONT_BODY, 0, 0, UI_WIDTH_TEXT, LV_TEXT_FLAG_NONE);
    ui_unlock();
    const int32_t n = size.y / lv_font_get_line_height(UI_FONT_BODY);
    return (uint16_t)(n < 1 ? 1 : n);
}

static void apply_tabs(const ui_question_t *q)
{
    ui_tabs_t t = {0};
    if (q->sent) {
        ui_screen_set_tabs(UI_SCREEN_QUESTION, &t);
        return;
    }
    const ui_tab_t back = {.state = UI_TAB_CLICK, .label = q->index > 1 ? "Back" : "Terminal"};
    switch (q->view) {
    case CARD_VIEW_MENU:
        t.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Back"};
        t.tab[UI_TAB_ACTION] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Next"};
        t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Choose"};
        break;
    case CARD_VIEW_READ:
        t.tab[UI_TAB_REJECT] = back;
        t.tab[UI_TAB_ACTION] = (ui_tab_t){.state = UI_TAB_CLICK_AND_HOLD,
                                          .label = q->page < q->pages ? "Page" : "Options", .hold = "More"};
        break;
    case CARD_VIEW_REVIEW:
        t.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Back"};
        t.tab[UI_TAB_ACTION] = (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "More"};
        t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Send"};
        break;
    case CARD_VIEW_CARD: {
        t.tab[UI_TAB_REJECT] = back;
        t.tab[UI_TAB_ACTION] = (ui_tab_t){.state = UI_TAB_CLICK_AND_HOLD, .label = "Next", .hold = "More"};
        const char *choose = "Choose";
        if (q->multi) {
            choose = q->opt >= q->n_options              ? "Send"
                     : q->marks[q->opt] == UI_OPT_TICKED ? "Untick"
                                                         : "Tick";
        }
        t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = choose};
        break;
    }
    }
    if (!q->armed) {
        for (int i = 0; i < UI_TAB_COUNT; i++) {
            ui_tab_t *tab = &t.tab[i];
            if (tab->state == UI_TAB_HOLD_ONLY) {
                tab->label = "Hold: More";
            }
            if (tab->state != UI_TAB_NONE) {
                tab->state = UI_TAB_INACTIVE;
            }
        }
    }
    ui_screen_set_tabs(UI_SCREEN_QUESTION, &t);
}

static const char *mark_icon(ui_opt_mark_t m)
{
    switch (m) {
    case UI_OPT_CHOSEN:
        return UI_ICON_CHECK;
    case UI_OPT_BOX:
        return UI_ICON_BOX0;
    case UI_OPT_TICKED:
        return UI_ICON_BOX1;
    case UI_OPT_PLAIN:
        break;
    }
    return NULL;
}

static void apply_card(const ui_question_t *q)
{
    /* A question that had a read page is one line above its options (33b); otherwise as many as it takes, up to two. */
    int8_t lines = 1;
    if (!q->read_first && q->text != NULL) {
        lv_point_t size;
        lv_text_get_size(&size, q->text, UI_FONT_BODY, 0, 0, UI_WIDTH_TEXT, LV_TEXT_FLAG_NONE);
        lines = size.y > lv_font_get_line_height(UI_FONT_BODY) ? CARD_Q_LINES : 1;
    }
    if (lines != s_text_lines) {
        const int32_t line_h = lv_font_get_line_height(UI_FONT_BODY);
        lv_obj_set_height(s_text, lines * line_h);
        const int32_t y = lines == 1 ? LIST_Y_SHORT : LIST_Y;
        w_list_move(s_options, y, LIST_BOTTOM - y);
        s_text_lines = lines;
    }
    ui_label_set(s_text, q->text);

    static char send[32];
    w_list_row_t rows[PROTO_Q_OPTIONS_MAX + 1];
    const uint8_t n = q->n_options <= PROTO_Q_OPTIONS_MAX ? q->n_options : PROTO_Q_OPTIONS_MAX;
    for (uint8_t i = 0; i < n; i++) {
        rows[i] = (w_list_row_t){
            .icon = mark_icon(q->marks[i]),
            .label = q->labels[i],
            .desc = q->descs[i],
            .accent = q->marks[i] == UI_OPT_CHOSEN || q->marks[i] == UI_OPT_TICKED,
        };
    }
    uint8_t count = n;
    if (q->multi) {
        if (q->ticked > 0) {
            snprintf(send, sizeof(send), "Send %u selected", q->ticked);
        } else {
            strlcpy(send, "None of these", sizeof(send));
        }
        rows[count++] = (w_list_row_t){.icon = UI_ICON_CHECK, .label = send};
    }
    w_list_set(s_options, rows, count, q->opt, W_LIST_FOCUS_CREAM);
}

static void apply_read(const ui_question_t *q)
{
    ui_label_set(s_read_text, q->text);
    const int32_t line_h = lv_font_get_line_height(UI_FONT_BODY);
    const int32_t y = -(int32_t)(q->page > 0 ? q->page - 1 : 0) * CARD_READ_LINES * line_h;
    if (lv_obj_get_y(s_read_text) != y) {
        lv_obj_set_y(s_read_text, y);
    }
    char note[48];
    if (q->pages > 1) {
        snprintf(note, sizeof(note), "%u/%u \xC2\xB7 %u options follow", q->page, q->pages, q->n_options);
    } else {
        snprintf(note, sizeof(note), "%u options follow", q->n_options);
    }
    ui_label_set(s_read_note, note);
}

static void apply_menu(const ui_question_t *q)
{
    static const w_list_row_t ROWS[] = {
        {.icon = UI_ICON_TERMINAL, .label = "Answer in terminal",
         .desc = "Moves to your keyboard. You can type your own answer there."},
        {.icon = UI_ICON_Q, .label = "Read the question again"},
        {.icon = UI_ICON_PET, .label = "Let Claude decide", .desc = "Every question not answered yet: no preference."},
        {.icon = UI_ICON_X, .label = "Decline to answer", .desc = "Claude goes on without your answer."},
    };
    w_list_set(s_menu_list, ROWS, sizeof(ROWS) / sizeof(ROWS[0]), q->cursor, W_LIST_FOCUS_CREAM);
}

static void apply_review(const ui_question_t *q)
{
    const uint8_t n = q->answers <= PROTO_Q_MAX ? q->answers : PROTO_Q_MAX;
    /* Two answers fit as two-line wells (37); three or four take one line each. */
    const int8_t compact = n > 2 ? 1 : 0;
    if (compact != s_review_short) {
        const int32_t h = compact ? WELL_H_SHORT : WELL_H;
        const int32_t small_h = lv_font_get_line_height(UI_FONT_SMALL);
        const int32_t body_h = lv_font_get_line_height(UI_FONT_BODY);
        for (int i = 0; i < PROTO_Q_MAX; i++) {
            lv_obj_set_y(s_wells[i], REVIEW_Y + i * (h + REVIEW_GAP));
            lv_obj_set_height(s_wells[i], h);
            if (compact) {
                lv_obj_set_pos(s_well_label[i], WELL_PAD, (h - small_h) / 2);
                lv_obj_set_width(s_well_label[i], WELL_LABEL_W);
                lv_obj_set_pos(s_well_answer[i], WELL_PAD + WELL_LABEL_W + HEAD_GAP, (h - body_h) / 2);
                lv_obj_set_width(s_well_answer[i], UI_WIDTH_SURFACE - 2 * WELL_PAD - WELL_LABEL_W - HEAD_GAP);
            } else {
                lv_obj_set_pos(s_well_label[i], WELL_PAD, WELL_PAD - 2);
                lv_obj_set_width(s_well_label[i], UI_WIDTH_SURFACE - 2 * WELL_PAD);
                lv_obj_set_pos(s_well_answer[i], WELL_PAD, WELL_PAD + small_h);
                lv_obj_set_width(s_well_answer[i], UI_WIDTH_SURFACE - 2 * WELL_PAD);
            }
        }
        s_review_short = compact;
    }
    for (uint8_t i = 0; i < PROTO_Q_MAX; i++) {
        ui_set_hidden(s_wells[i], i >= n);
        if (i < n) {
            ui_label_set_caps(s_well_label[i], q->review_label[i]);
            ui_label_set(s_well_answer[i], q->review_answer[i]);
        }
    }
}

static void apply(const ui_question_t *q)
{
    static char right[24];
    static char title[32];
    /* The position belongs to a question; the menu and the review show the time only (36, 37). */
    if (q->count > 1 && (q->view == CARD_VIEW_CARD || q->view == CARD_VIEW_READ)) {
        snprintf(right, sizeof(right), "%u/%u \xC2\xB7 %u s", q->index, q->count, q->remaining_s);
    } else {
        snprintf(right, sizeof(right), "%u s", q->remaining_s);
    }
    ui_label_set(s_head_right, right);
    const bool menu = q->view == CARD_VIEW_MENU;
    ui_set_hidden(s_head_caps, menu);
    ui_set_hidden(s_head_text, !menu);
    if (menu) {
        ui_label_set(s_head_text, q->text);
    } else if (q->view == CARD_VIEW_REVIEW) {
        snprintf(title, sizeof(title), "Send %u answer%s?", q->answers, q->answers == 1 ? "" : "s");
        ui_label_set_caps(s_head_caps, title);
    } else {
        ui_label_set_caps(s_head_caps, q->header != NULL ? q->header : "");
    }
    ui_set_hidden(s_card, q->view != CARD_VIEW_CARD);
    ui_set_hidden(s_read, q->view != CARD_VIEW_READ);
    ui_set_hidden(s_menu, !menu);
    ui_set_hidden(s_review, q->view != CARD_VIEW_REVIEW);
    switch (q->view) {
    case CARD_VIEW_CARD:
        apply_card(q);
        break;
    case CARD_VIEW_READ:
        apply_read(q);
        break;
    case CARD_VIEW_MENU:
        apply_menu(q);
        break;
    case CARD_VIEW_REVIEW:
        apply_review(q);
        break;
    }
    ui_set_hidden(s_sent, !q->sent);
    w_countdown_set(s_bar, q->remaining_permille, -1, UI_C_SKY);
    apply_tabs(q);
}

void ui_question_set(const ui_question_t *q)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active_locked()) {
        apply(q);
    }
    ui_unlock();
}

void screen_question_demo(int demo)
{
    if (demo == QUESTION_DEMO_OFF) {
        return;
    }
    static const char *const LABELS[] = {"Vitest (Recommended)", "Node test runner", "Jest", "Mocha"};
    static const char *const DESCS[] = {"Already used by the server and client packages, so config and CI stay the same.",
                                        "Built into Node, no dependency.", "", ""};
    ui_question_t q = {
        .view = CARD_VIEW_CARD,
        .header = "Test runner",
        .text = "Which test runner should the new package use?",
        .index = 1,
        .count = 1,
        .remaining_permille = 433,
        .remaining_s = 26,
        .n_options = 2,
        .armed = true,
    };
    for (int i = 0; i < 4; i++) {
        q.labels[i] = LABELS[i];
        q.descs[i] = DESCS[i];
    }
    switch ((ui_question_demo_t)demo) {
    case QUESTION_DEMO_OFF:
    case QUESTION_DEMO_SINGLE:
        break;
    case QUESTION_DEMO_SECOND:
        q.header = "Storage";
        q.text = "Where should paired devices be stored?";
        q.index = 1;
        q.count = 2;
        q.remaining_s = 41;
        q.remaining_permille = 683;
        q.labels[0] = "SQLite (Recommended)";
        q.labels[1] = "Postgres";
        q.descs[1] = "Scales to a hosted multi-user server, but local runs need a container and a connection string.";
        q.opt = 1;
        break;
    case QUESTION_DEMO_BACK:
        q.header = "Auth method";
        q.text = "How should the dashboard authenticate to the server?";
        q.index = 2;
        q.count = 2;
        q.remaining_s = 52;
        q.remaining_permille = 866;
        q.n_options = 3;
        q.labels[0] = "Bearer token (Recommended)";
        q.descs[0] = "Reuses the CLI token the hooks already send. Simplest, but the token sits in localStorage.";
        q.labels[1] = "Signed session cookie";
        q.labels[2] = "Device code login";
        q.marks[0] = UI_OPT_CHOSEN;
        break;
    case QUESTION_DEMO_READ:
    case QUESTION_DEMO_SCROLL:
        q.header = "Migration";
        q.text = "The devices table gained a room_id column, so the schema changed. How should existing rooms and "
                 "their paired devices be migrated when the server starts?";
        q.n_options = 4;
        q.labels[0] = "Copy to a new file";
        q.descs[0] = "Build devices_v2.db next to the old one. Slower, but the old file stays as a backup.";
        q.labels[1] = "Migrate in place";
        q.labels[2] = "Start fresh";
        q.labels[3] = "Ask me again later";
        q.remaining_s = 30;
        q.remaining_permille = 500;
        q.page = 1;
        q.pages = 1;
        q.view = demo == QUESTION_DEMO_READ ? CARD_VIEW_READ : CARD_VIEW_CARD;
        q.read_first = true;
        break;
    case QUESTION_DEMO_MULTI:
    case QUESTION_DEMO_SEND:
        q.header = "Deliver";
        q.text = "What should this change include?";
        q.multi = true;
        q.n_options = 4;
        q.labels[0] = "Unit tests";
        q.labels[1] = "End-to-end tests";
        q.descs[0] = "";
        q.descs[1] = "Server, client and the sim device together.";
        q.labels[2] = "Docs";
        q.labels[3] = "Changelog";
        q.marks[0] = UI_OPT_TICKED;
        q.marks[1] = UI_OPT_TICKED;
        q.marks[2] = UI_OPT_BOX;
        q.marks[3] = UI_OPT_BOX;
        q.ticked = 2;
        q.opt = demo == QUESTION_DEMO_MULTI ? 1 : 4;
        q.remaining_s = demo == QUESTION_DEMO_MULTI ? 38 : 33;
        q.remaining_permille = 633;
        break;
    case QUESTION_DEMO_MENU:
        q.view = CARD_VIEW_MENU;
        q.text = "Where should paired devices be stored?";
        q.remaining_s = 44;
        q.remaining_permille = 733;
        break;
    case QUESTION_DEMO_REVIEW:
        q.view = CARD_VIEW_REVIEW;
        q.answers = 2;
        q.review_label[0] = "Storage";
        q.review_answer[0] = "Postgres";
        q.review_label[1] = "Auth method";
        q.review_answer[1] = "Bearer token (Recommended)";
        q.index = 2;
        q.count = 2;
        q.remaining_s = 57;
        q.remaining_permille = 950;
        break;
    case QUESTION_DEMO_ACCENTS:
        /* Every dynamic field with Latin-1 letters and a dash: no glyph may be missing. */
        q.header = "D\xC3\xA9j\xC3\xA0 vu";
        q.text = "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9?";
        q.labels[0] = "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9?";
        q.descs[0] = "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9? \xC3\x85ngstr\xC3\xB6m \xC3\x9F \xC3\xB1";
        q.labels[1] = "Cr\xC3\xA8me br\xC3\xBBl\xC3\xA9" "e \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\xA6";
        break;
    case QUESTION_DEMO_ACCENTS_REVIEW:
        q.view = CARD_VIEW_REVIEW;
        q.answers = 2;
        q.review_label[0] = "D\xC3\xA9j\xC3\xA0 vu";
        q.review_answer[0] = "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9?";
        q.review_label[1] = "\xC3\xA9t\xC3\xA9 \xC3\xA0 la plage";
        q.review_answer[1] = "Cr\xC3\xA8me br\xC3\xBBl\xC3\xA9" "e, \xC3\x85ngstr\xC3\xB6m";
        q.index = 2;
        q.count = 2;
        break;
    }
    ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
    apply(&q);
}
