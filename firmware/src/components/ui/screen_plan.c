#include "ui_priv.h"

#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Layout of the mockups: plan card (29) and plan menu (29b). */
#define HEAD_Y        25
#define HEAD_H        22
#define HEAD_GAP      8
#define TITLE_Y       51
#define WELL_Y        78
#define WELL_PAD      8
#define STEP_H        21
/* A lossy plan needs a notice line under the well: its steps sit closer together. */
#define WELL_PAD_CUT  6
#define STEP_H_CUT    19
#define NOTICE_GAP    2
#define NUM_X         10
#define STEP_X        30
#define FOOT_Y        185
#define FOOT_W        220
#define BAR_Y         208
#define SENT_Y        220
#define LIST_Y        50
#define LIST_H        152
#define ROW_H         25

static lv_obj_t *s_head_caps;
static lv_obj_t *s_head_text;
static lv_obj_t *s_head_right;

static lv_obj_t *s_card;
static lv_obj_t *s_title;
static lv_obj_t *s_well;
static lv_obj_t *s_num[CARD_PLAN_STEPS];
static lv_obj_t *s_step[CARD_PLAN_STEPS];
static lv_obj_t *s_notice;
static lv_obj_t *s_foot;
static w_countdown_t *s_bar;
/* Guarded by the ui lock: the step pitch last laid out. */
static int32_t s_step_h;
static lv_obj_t *s_sent;

static lv_obj_t *s_menu;
static w_list_t *s_list;
static w_countdown_t *s_menu_bar;

lv_obj_t *screen_plan_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *head = ui_box(scr, UI_PAD, HEAD_Y, UI_WIDTH_SURFACE, HEAD_H);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, HEAD_GAP, 0);
    w_chip_t *chip = w_chip_create(head, 0, 0);
    w_chip_set(chip, UI_ICON_LIST, "Plan", UI_C_SKY, 0, NULL);
    s_head_caps = ui_text(head, UI_FONT_SMALL, UI_C_SKY, 0, 0, 0, true);
    lv_obj_add_style(s_head_caps, &ui_style_caps, 0);
    lv_obj_set_style_text_color(s_head_caps, UI_C_SKY, 0);
    lv_obj_set_flex_grow(s_head_caps, 1);
    lv_label_set_text_static(s_head_caps, "READY TO BUILD");
    s_head_text = ui_text(head, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);
    lv_label_set_long_mode(s_head_text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_head_text, 1);
    s_head_right = ui_text(head, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);

    s_card = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    s_title = ui_text(s_card, UI_FONT_BODY, UI_C_CREAM, UI_TEXT_X, TITLE_Y, UI_WIDTH_TEXT, true);
    s_well = ui_box(s_card, UI_PAD, WELL_Y, UI_WIDTH_SURFACE, 2 * WELL_PAD + CARD_PLAN_STEPS * STEP_H);
    lv_obj_add_style(s_well, &ui_style_well, 0);
    for (int i = 0; i < CARD_PLAN_STEPS; i++) {
        s_num[i] = ui_text(s_well, UI_FONT_MONO, UI_C_SLATE, NUM_X, 0, 0, true);
        s_step[i] = ui_text(s_well, UI_FONT_SMALL, UI_C_CREAM, STEP_X, 0, UI_WIDTH_SURFACE - STEP_X - NUM_X, true);
    }
    s_notice = ui_text_line(s_card, UI_FONT_SMALL, UI_C_AMBER, 0, LV_TEXT_ALIGN_LEFT);
    s_foot = ui_text(s_card, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, FOOT_Y, FOOT_W, true);
    lv_label_set_recolor(s_foot, true);
    s_bar = w_countdown_create(s_card, BAR_Y, true);
    s_sent = ui_text_line(s_card, UI_FONT_SMALL, UI_C_FOG, SENT_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(s_sent, "Sent\xE2\x80\xA6");
    lv_obj_add_flag(s_sent, LV_OBJ_FLAG_HIDDEN);

    s_menu = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    s_list = w_list_create(s_menu, LIST_Y, LIST_H, ROW_H);
    s_menu_bar = w_countdown_create(s_menu, BAR_Y, false);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    return scr;
}

static void apply_tabs(const ui_plan_t *p, card_row_t focused)
{
    ui_tabs_t t = {0};
    if (p->sent) {
        ui_screen_set_tabs(UI_SCREEN_PLAN, &t);
        return;
    }
    if (p->view == CARD_VIEW_MENU) {
        t.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Back"};
        t.tab[UI_TAB_ACTION] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Next"};
        if (focused != CARD_ROW_ACCEPT_EDITS) {
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Choose"};
        } else if (p->edits == CARD_SESSION_READY) {
            t.tab[UI_TAB_APPROVE] =
                (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "Approve", .holding = "Approving\xE2\x80\xA6"};
        } else {
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_INACTIVE, .label = "Approve"};
        }
    } else {
        t.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Revise"};
        t.tab[UI_TAB_ACTION] = p->pages > 1 ? (ui_tab_t){.state = UI_TAB_CLICK_AND_HOLD, .label = "Page", .hold = "More"}
                                            : (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "More"};
        if (p->approve == CARD_ALLOW_CLICK) {
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Approve"};
        } else if (p->approve == CARD_ALLOW_INACTIVE) {
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_INACTIVE, .label = "Approve"};
        }
    }
    if (!p->armed) {
        for (int i = 0; i < UI_TAB_COUNT; i++) {
            ui_tab_t *tab = &t.tab[i];
            if (tab->state == UI_TAB_HOLD_ONLY) {
                tab->label = i == UI_TAB_APPROVE ? "Hold: Approve" : "Hold: More";
            }
            if (tab->state != UI_TAB_NONE) {
                tab->state = UI_TAB_INACTIVE;
            }
        }
    }
    ui_screen_set_tabs(UI_SCREEN_PLAN, &t);
}

static void apply_card(const ui_plan_t *p)
{
    ui_label_set(s_title, p->title);
    const uint8_t first = (uint8_t)((p->page > 0 ? p->page - 1 : 0) * CARD_PLAN_STEPS);
    for (uint8_t i = 0; i < CARD_PLAN_STEPS; i++) {
        const uint8_t n = (uint8_t)(first + i);
        const bool shown = n < p->n_steps && p->steps != NULL;
        ui_set_hidden(s_num[i], !shown);
        ui_set_hidden(s_step[i], !shown);
        if (shown) {
            char num[4];
            snprintf(num, sizeof(num), "%u", n + 1);
            ui_label_set(s_num[i], num);
            ui_label_set(s_step[i], p->steps[n]);
        }
    }
    /* A lossy summary says so under the well on every page (protocol 14.5). */
    const bool notice = p->more > 0 || p->cut;
    const int32_t step_h = notice ? STEP_H_CUT : STEP_H;
    if (step_h != s_step_h) {
        const int32_t pad = notice ? WELL_PAD_CUT : WELL_PAD;
        const int32_t well_h = 2 * pad + CARD_PLAN_STEPS * step_h;
        lv_obj_set_height(s_well, well_h);
        for (int i = 0; i < CARD_PLAN_STEPS; i++) {
            const int32_t y = pad + i * step_h + (step_h - lv_font_get_line_height(UI_FONT_SMALL)) / 2;
            lv_obj_set_y(s_num[i], y);
            lv_obj_set_y(s_step[i], y);
        }
        lv_obj_set_y(s_notice, WELL_Y + well_h + NOTICE_GAP);
        s_step_h = step_h;
    }
    static char line[64];
    if (p->more > 0) {
        snprintf(line, sizeof(line), "+%u more step%s \xC2\xB7 read it in the terminal", p->more,
                 p->more == 1 ? "" : "s");
    } else {
        strlcpy(line, notice ? "Shortened \xC2\xB7 full plan in the terminal" : "", sizeof(line));
    }
    ui_set_hidden(s_notice, !notice);
    ui_label_set(s_notice, line);
    static char foot[64];
    snprintf(foot, sizeof(foot), "%s " UI_RECOLOR_SLATE " \xC2\xB7 %u step%s#", p->project != NULL ? p->project : "",
             p->n_steps + p->more, p->n_steps + p->more == 1 ? "" : "s");
    ui_label_set(s_foot, foot);
    ui_set_hidden(s_sent, !p->sent);
    w_countdown_set(s_bar, p->remaining_permille, p->remaining_s, UI_C_SKY);
}

static void apply_menu(const ui_plan_t *p, card_row_t focused)
{
    w_list_row_t rows[CARD_MENU_ROWS];
    const uint8_t n = p->rows <= CARD_MENU_ROWS ? p->rows : CARD_MENU_ROWS;
    for (uint8_t i = 0; i < n; i++) {
        w_list_row_t *r = &rows[i];
        *r = (w_list_row_t){0};
        switch (p->row[i]) {
        case CARD_ROW_ACCEPT_EDITS:
            r->icon = UI_ICON_WARN;
            r->label = "Approve, accept all edits";
            if (p->edits == CARD_SESSION_READY) {
                r->value = focused == CARD_ROW_ACCEPT_EDITS ? NULL : "HOLD";
                r->desc = "Claude may change any file in this project without asking, until it exits.";
            } else {
                r->muted = true;
                r->desc = "Read every page first";
            }
            break;
        case CARD_ROW_DENY_STOP:
            *r = (w_list_row_t){.icon = UI_ICON_STOP, .label = "Deny and stop",
                                .desc = "Ends Claude's turn. Nothing else runs."};
            break;
        case CARD_ROW_TERMINAL:
        default:
            *r = (w_list_row_t){.icon = UI_ICON_TERMINAL, .label = "Send to terminal",
                                .desc = "Read the whole plan and decide at the keyboard."};
            break;
        }
    }
    w_list_focus_t style = W_LIST_FOCUS_CREAM;
    if (focused == CARD_ROW_ACCEPT_EDITS) {
        style = p->edits == CARD_SESSION_READY ? W_LIST_FOCUS_AMBER : W_LIST_FOCUS_MUTED;
    }
    w_list_set(s_list, rows, n, p->cursor, style);
    w_countdown_set(s_menu_bar, p->remaining_permille, -1, UI_C_SKY);
}

static void apply(const ui_plan_t *p)
{
    const bool menu = p->view == CARD_VIEW_MENU && !p->sent;
    const card_row_t focused = p->rows > 0 && p->cursor < p->rows ? p->row[p->cursor] : CARD_ROW_TERMINAL;
    char right[16] = "";
    if (menu) {
        snprintf(right, sizeof(right), "%u s", p->remaining_s);
    } else if (p->pages > 1) {
        snprintf(right, sizeof(right), "%u/%u", p->page, p->pages);
    }
    ui_label_set(s_head_right, right);
    ui_set_hidden(s_head_caps, menu);
    ui_set_hidden(s_head_text, !menu);
    ui_set_hidden(s_card, menu);
    ui_set_hidden(s_menu, !menu);
    if (menu) {
        ui_label_set(s_head_text, p->title);
        apply_menu(p, focused);
    } else {
        apply_card(p);
    }
    apply_tabs(p, focused);
}

void ui_plan_set(const ui_plan_t *p)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(p);
    }
    ui_unlock();
}

void screen_plan_demo(int demo)
{
    if (demo == PLAN_DEMO_OFF) {
        return;
    }
    static const char STEPS[][PROTO_PLAN_STEP_SIZE] = {
        "Widen the hook reply type in shared", "Send questions one at a time", "New question screen in firmware",
        "Dashboard mirror and e2e tests",      "Plan card and plan menu",      "Receipts on home",
        "Docs and the protocol section",
    };
    static const char ACCENTS[][PROTO_PLAN_STEP_SIZE] = {
        "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9?",
        "\xC3\x85ngstr\xC3\xB6m, \xC3\x9F, \xC3\xB1, \xC3\xA6, \xC3\xBF \xE2\x80\xA6",
    };
    ui_plan_t p = {
        .view = CARD_VIEW_CARD,
        .title = "Add question cards to the device",
        .steps = STEPS,
        .n_steps = 7,
        .page = 1,
        .pages = 2,
        .project = "devotchi",
        .remaining_permille = 800,
        .remaining_s = 48,
        .approve = CARD_ALLOW_INACTIVE,
        .edits = CARD_SESSION_LOCKED,
        .rows = 3,
        .row = {CARD_ROW_TERMINAL, CARD_ROW_ACCEPT_EDITS, CARD_ROW_DENY_STOP},
        .armed = true,
    };
    switch ((ui_plan_demo_t)demo) {
    case PLAN_DEMO_OFF:
    case PLAN_DEMO_CARD:
        break;
    case PLAN_DEMO_MENU:
        p.view = CARD_VIEW_MENU;
        p.approve = CARD_ALLOW_CLICK;
        p.edits = CARD_SESSION_READY;
        p.cursor = 1;
        p.remaining_s = 41;
        p.remaining_permille = 683;
        break;
    case PLAN_DEMO_CUT:
        p.cut = true;
        p.page = 2;
        p.approve = CARD_ALLOW_CLICK;
        break;
    case PLAN_DEMO_MORE:
        p.cut = true;
        p.more = 5;
        p.n_steps = 4;
        p.pages = 1;
        p.approve = CARD_ALLOW_NONE;
        break;
    case PLAN_DEMO_ACCENTS:
        p.title = "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9?";
        p.project = "caf\xC3\xA9";
        p.steps = ACCENTS;
        p.n_steps = 2;
        p.pages = 1;
        p.approve = CARD_ALLOW_CLICK;
        break;
    }
    ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
    apply(&p);
}
