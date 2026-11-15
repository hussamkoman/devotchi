#include "ui_priv.h"

#include "text.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Layout of the mockups: the card (20-24) and the decision menu (25-27). */
#define CHIP_Y        25
#define QUEUE_W       60
#define WELL_Y        51
#define REASON_GAP    6
#define REASON_H      18
#define REASON_TEXT_X 32
#define WORDS_GAP     8
#define FOOT_Y        185
#define BAR_Y         208
#define SENT_Y        220
#define FOOT_W        220
#define HEAD_GAP      8
#define HEAD_SECS_W   40
#define LIST_Y        50
#define LIST_H        152
#define ROW_H         25

static lv_obj_t *s_card;
static w_chip_t *s_chip;
static lv_obj_t *s_queue;
static w_well_t *s_well;
static lv_obj_t *s_reason;
static lv_obj_t *s_reason_icon;
static lv_obj_t *s_reason_text;
static lv_obj_t *s_words;
static lv_obj_t *s_foot;
static w_countdown_t *s_bar;
static lv_obj_t *s_sent;

static lv_obj_t *s_menu;
static w_chip_t *s_menu_chip;
static lv_obj_t *s_menu_cmd;
static lv_obj_t *s_menu_secs;
static w_list_t *s_list;
static w_countdown_t *s_menu_bar;

/* Guarded by the ui lock. */
static int32_t s_words_y = -1;
static uint8_t s_words_lines;

lv_obj_t *screen_approval_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    s_card = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    s_chip = w_chip_create(s_card, UI_PAD, CHIP_Y);
    s_queue = ui_text(s_card, UI_FONT_SMALL, UI_C_FOG, LV_HOR_RES - UI_PAD - QUEUE_W, CHIP_Y + 2, QUEUE_W, true);
    lv_obj_set_style_text_align(s_queue, LV_TEXT_ALIGN_RIGHT, 0);
    s_well = w_well_create(s_card, WELL_Y);
    s_reason = ui_box(s_card, UI_TEXT_X, WELL_Y, UI_WIDTH_TEXT, REASON_H);
    s_reason_icon = ui_icon(s_reason, UI_ICON_WARN, UI_C_CORAL, 0, 2);
    s_reason_text = ui_text(s_reason, UI_FONT_SMALL, UI_C_CORAL, REASON_TEXT_X - UI_TEXT_X, 0,
                            UI_WIDTH_TEXT - (REASON_TEXT_X - UI_TEXT_X), true);
    /* Recolouring marks "Claude:" as the label of Claude's own words. */
    s_words = ui_text(s_card, UI_FONT_BODY, UI_C_FOG, UI_TEXT_X, WELL_Y, UI_WIDTH_TEXT, false);
    lv_label_set_long_mode(s_words, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_recolor(s_words, true);
    s_foot = ui_text(s_card, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, FOOT_Y, FOOT_W, true);
    lv_label_set_recolor(s_foot, true);
    s_bar = w_countdown_create(s_card, BAR_Y, true);
    s_sent = ui_text_line(s_card, UI_FONT_SMALL, UI_C_FOG, SENT_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(s_sent, "Sent\xE2\x80\xA6");
    lv_obj_add_flag(s_sent, LV_OBJ_FLAG_HIDDEN);

    s_menu = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    lv_obj_t *head = ui_box(s_menu, UI_PAD, CHIP_Y, UI_WIDTH_SURFACE, 22);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, HEAD_GAP, 0);
    s_menu_chip = w_chip_create(head, 0, 0);
    s_menu_cmd = ui_text(head, UI_FONT_MONO, UI_C_FOG, 0, 0, 0, true);
    lv_label_set_long_mode(s_menu_cmd, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_menu_cmd, 1);
    s_menu_secs = ui_text(head, UI_FONT_SMALL, UI_C_FOG, 0, 0, HEAD_SECS_W, true);
    lv_obj_set_style_text_align(s_menu_secs, LV_TEXT_ALIGN_RIGHT, 0);
    s_list = w_list_create(s_menu, LIST_Y, LIST_H, ROW_H);
    s_menu_bar = w_countdown_create(s_menu, BAR_Y, false);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    return scr;
}

static lv_color_t risk_color(proto_risk_t risk)
{
    switch (risk) {
    case PROTO_RISK_LOW:
        return UI_C_MINT;
    case PROTO_RISK_MED:
        return UI_C_AMBER;
    case PROTO_RISK_HIGH:
        break;
    }
    return UI_C_CORAL;
}

static const char *risk_words(proto_risk_t risk)
{
    switch (risk) {
    case PROTO_RISK_LOW:
        return "Low risk";
    case PROTO_RISK_MED:
        return "Medium risk";
    case PROTO_RISK_HIGH:
        break;
    }
    return "High risk";
}

/* The chip keeps the tool's icon (design-system 7). */
static const char *tool_icon(const char *tool)
{
    if (tool == NULL) {
        return UI_ICON_TERMINAL;
    }
    if (strcmp(tool, "Edit") == 0 || strcmp(tool, "Write") == 0 || strcmp(tool, "MultiEdit") == 0 ||
        strcmp(tool, "NotebookEdit") == 0) {
        return UI_ICON_PENCIL;
    }
    if (strncmp(tool, "Web", 3) == 0) {
        return UI_ICON_GLOBE;
    }
    if (strncmp(tool, "mcp__", 5) == 0) {
        return UI_ICON_PLUG;
    }
    return UI_ICON_TERMINAL;
}

/* Tabs of ux-flows 2; while the card arms every tab is slate. */
static void apply_tabs(const ui_approval_t *a, card_row_t focused)
{
    ui_tabs_t t = {0};
    if (a->sent) {
        ui_screen_set_tabs(UI_SCREEN_APPROVAL, &t);
        return;
    }
    if (a->view == CARD_VIEW_MENU) {
        t.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Back"};
        t.tab[UI_TAB_ACTION] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Next"};
        if (focused != CARD_ROW_SESSION) {
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Choose"};
        } else if (a->session == CARD_SESSION_READY) {
            t.tab[UI_TAB_APPROVE] =
                (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "Allow", .holding = "Allowing\xE2\x80\xA6"};
        } else {
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_INACTIVE, .label = "Allow"};
        }
    } else {
        t.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Deny"};
        t.tab[UI_TAB_ACTION] = a->pages > 1 ? (ui_tab_t){.state = UI_TAB_CLICK_AND_HOLD, .label = "Page", .hold = "More"}
                                            : (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "More"};
        switch (a->allow) {
        case CARD_ALLOW_NONE:
            break;
        case CARD_ALLOW_INACTIVE:
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_INACTIVE, .label = "Allow"};
            break;
        case CARD_ALLOW_CLICK:
            t.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Allow"};
            break;
        case CARD_ALLOW_HOLD:
            t.tab[UI_TAB_APPROVE] =
                (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "Allow", .holding = "Allowing\xE2\x80\xA6"};
            break;
        }
    }
    if (!a->armed) {
        for (int i = 0; i < UI_TAB_COUNT; i++) {
            ui_tab_t *tab = &t.tab[i];
            if (tab->state == UI_TAB_HOLD_ONLY) {
                tab->label = i == UI_TAB_APPROVE ? "Hold: Allow" : "Hold: More";
            }
            if (tab->state != UI_TAB_NONE) {
                tab->state = UI_TAB_INACTIVE;
            }
        }
    }
    ui_screen_set_tabs(UI_SCREEN_APPROVAL, &t);
}

/* `Claude: "words"` with the label in slate; '#' starts a recolour command, so it is doubled. */
static void words_text(char *dst, size_t cap, const char *words, bool claude)
{
    static char clean[UI_LABEL_TEXT_SIZE];
    text_sanitize_device(clean, sizeof(clean), words);
    size_t n = (size_t)snprintf(dst, cap, "%s", claude ? UI_RECOLOR_SLATE " Claude:# \xE2\x80\x9C" : "");
    for (const char *p = clean; *p != '\0' && n + 5 < cap; p++) {
        if (*p == '#') {
            dst[n++] = '#';
        }
        dst[n++] = *p;
    }
    strlcpy(dst + n, claude ? "\xE2\x80\x9D" : "", cap - n);
}

static void apply_card(const ui_approval_t *a, lv_color_t color)
{
    w_chip_set(s_chip, tool_icon(a->tool), a->tool, color, (uint8_t)(a->risk + 1), risk_words(a->risk));
    ui_set_hidden(s_sent, !a->sent);

    char line[48] = "";
    if (a->count > 1) {
        snprintf(line, sizeof(line), "%u/%u", a->position, a->count);
    }
    ui_label_set(s_queue, line);

    const size_t lines = a->pages > 0 ? text_wrap_lines(a->details, CARD_COLS) : 0;
    const uint8_t shown = (uint8_t)(lines > CARD_LINES ? CARD_LINES : lines);
    char more[32] = "";
    char page[16] = "";
    const bool paged = a->pages > 1;
    if (paged) {
        if (a->lines_after > 0) {
            snprintf(more, sizeof(more), "+%u more line%s", a->lines_after, a->lines_after == 1 ? "" : "s");
        } else if (a->allow == CARD_ALLOW_NONE) {
            strlcpy(more, "Too long to allow here", sizeof(more));
        } else if (strcmp(tool_icon(a->tool), UI_ICON_PENCIL) == 0 && a->cmd_line != NULL) {
            /* The last page of a file change names the file again. */
            const char *slash = strrchr(a->cmd_line, '/');
            strlcpy(more, slash != NULL ? slash + 1 : a->cmd_line, sizeof(more));
        }
        snprintf(page, sizeof(page), "%u/%u", a->page, a->pages);
    }
    const bool reason = a->reason != NULL && a->reason[0] != '\0';
    ui_set_hidden(w_well_obj(s_well), a->pages == 0);
    const int32_t well_h = a->pages > 0 ? w_well_set(s_well, a->details, shown, paged ? more : NULL,
                                                     paged ? page : NULL, reason, color)
                                        : 0;
    w_well_set_prompt(s_well, a->prompt);
    int32_t y = a->pages > 0 ? WELL_Y + well_h + REASON_GAP : WELL_Y;
    ui_set_hidden(s_reason, !reason);
    if (reason) {
        lv_obj_set_y(s_reason, y);
        lv_obj_set_style_text_color(s_reason_icon, color, 0);
        lv_obj_set_style_text_color(s_reason_text, color, 0);
        ui_label_set(s_reason_text, a->reason);
        y += REASON_H + REASON_GAP - 2;
    } else if (a->pages > 0) {
        y += WORDS_GAP - REASON_GAP;
    }

    /* The words take two lines when they fit above the footer, else one. */
    const int32_t line_h = lv_font_get_line_height(UI_FONT_BODY);
    const uint8_t words_lines = y + 2 * line_h <= FOOT_Y ? 2 : 1;
    if (y != s_words_y || words_lines != s_words_lines) {
        lv_obj_set_y(s_words, y);
        lv_obj_set_height(s_words, words_lines * line_h);
        s_words_y = y;
        s_words_lines = words_lines;
    }
    static char words[UI_LABEL_TEXT_SIZE];
    words_text(words, sizeof(words), a->words, a->words_claude);
    ui_set_hidden(s_words, a->words == NULL || a->words[0] == '\0');
    ui_label_set(s_words, words);

    static char foot[96];
    if (a->sid != NULL && a->sid[0] != '\0') {
        snprintf(foot, sizeof(foot), "%s " UI_RECOLOR_SLATE " \xC2\xB7 %s#", a->project, a->sid);
    } else {
        strlcpy(foot, a->project != NULL ? a->project : "", sizeof(foot));
    }
    ui_label_set(s_foot, foot);
    w_countdown_set(s_bar, a->remaining_permille, a->remaining_s, color);
}

static void apply_menu(const ui_approval_t *a, lv_color_t color, card_row_t focused)
{
    w_chip_set(s_menu_chip, tool_icon(a->tool), a->tool, color, 0, NULL);
    ui_label_set(s_menu_cmd, a->cmd_line);
    char secs[16];
    snprintf(secs, sizeof(secs), "%u s", a->remaining_s);
    ui_label_set(s_menu_secs, secs);

    static char rule[PROTO_RULE_SIZE + 8];
    snprintf(rule, sizeof(rule), "%s%s", a->rule != NULL ? a->rule : "", a->rule_prefix ? " \xE2\x80\xA6" : "");
    w_list_row_t rows[CARD_MENU_ROWS];
    const uint8_t n = a->rows <= CARD_MENU_ROWS ? a->rows : CARD_MENU_ROWS;
    for (uint8_t i = 0; i < n; i++) {
        w_list_row_t *r = &rows[i];
        *r = (w_list_row_t){0};
        switch (a->row[i]) {
        case CARD_ROW_TERMINAL:
            *r = (w_list_row_t){.icon = UI_ICON_TERMINAL, .label = "Send to terminal",
                                .desc = "Decide at the keyboard instead."};
            break;
        case CARD_ROW_SESSION:
            r->icon = UI_ICON_SHIELD;
            r->label = "Allow for this session";
            if (a->session == CARD_SESSION_READY) {
                r->value = focused == CARD_ROW_SESSION ? NULL : "HOLD";
                r->code = rule;
                r->desc = a->rule_prefix ? "Starts like this, until Claude exits." : "Exactly this command";
            } else {
                r->muted = true;
                r->desc = "Read the whole command first";
            }
            break;
        case CARD_ROW_DENY_ASK_WHY:
            *r = (w_list_row_t){.icon = UI_ICON_SPEECH, .label = "Deny, ask why",
                                .desc = "Claude explains why it needs this, then may ask again."};
            break;
        case CARD_ROW_DENY_STOP:
            *r = (w_list_row_t){.icon = UI_ICON_STOP, .label = "Deny and stop",
                                .desc = "Ends Claude's turn. Nothing else runs."};
            break;
        case CARD_ROW_READ_AGAIN:
        case CARD_ROW_LET_CLAUDE:
        case CARD_ROW_DECLINE:
        case CARD_ROW_ACCEPT_EDITS:
            break;
        }
    }
    w_list_focus_t style = W_LIST_FOCUS_CREAM;
    if (focused == CARD_ROW_SESSION) {
        style = a->session == CARD_SESSION_READY ? W_LIST_FOCUS_AMBER : W_LIST_FOCUS_MUTED;
    }
    w_list_set(s_list, rows, n, a->cursor, style);
    w_countdown_set(s_menu_bar, a->remaining_permille, -1, color);
}

static void apply(const ui_approval_t *a)
{
    const lv_color_t color = risk_color(a->risk);
    const bool menu = a->view == CARD_VIEW_MENU && !a->sent;
    const card_row_t focused = a->rows > 0 && a->cursor < a->rows ? a->row[a->cursor] : CARD_ROW_TERMINAL;
    ui_set_hidden(s_card, menu);
    ui_set_hidden(s_menu, !menu);
    if (menu) {
        apply_menu(a, color, focused);
    } else {
        apply_card(a, color);
    }
    apply_tabs(a, focused);
}

void ui_approval_set(const ui_approval_t *a)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(a);
    }
    ui_unlock();
}

void screen_approval_demo(int demo)
{
    ui_approval_t a = {
        .tool = "Bash",
        .risk = PROTO_RISK_LOW,
        .project = "devotchi",
        .sid = "3fa9c1",
        .words = "Run the server test suite",
        .words_claude = true,
        .reason = "",
        .details = "  pnpm test --filter server",
        .prompt = true,
        .position = 1,
        .count = 1,
        .page = 1,
        .pages = 1,
        .remaining_permille = 800,
        .remaining_s = 24,
        .armed = true,
        .allow = CARD_ALLOW_CLICK,
        .view = CARD_VIEW_CARD,
        .cmd_line = "pnpm test --filter server",
        .rows = 4,
        .row = {CARD_ROW_TERMINAL, CARD_ROW_SESSION, CARD_ROW_DENY_ASK_WHY, CARD_ROW_DENY_STOP},
        .session = CARD_SESSION_READY,
        .rule = "pnpm test",
        .rule_prefix = true,
    };
    ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
    const bool high = demo == APPROVAL_DEMO_HIGH || demo == APPROVAL_DEMO_CLICKED || demo == APPROVAL_DEMO_HOLDING ||
                      demo >= APPROVAL_DEMO_HOLD || demo == APPROVAL_DEMO_MENU_HIGH;
    if (high) {
        a.risk = PROTO_RISK_HIGH;
        a.allow = CARD_ALLOW_HOLD;
        a.words = "Update the remote branch";
        a.reason = "Force push: rewrites the remote branch";
        a.details = "  git push --force origin master";
        a.cmd_line = "git push --force origin master";
        a.remaining_permille = 900;
        a.remaining_s = 27;
        a.rows = 3;
        a.row[1] = CARD_ROW_DENY_ASK_WHY;
        a.row[2] = CARD_ROW_DENY_STOP;
        a.session = CARD_SESSION_ABSENT;
    }
    switch ((ui_approval_demo_t)demo) {
    case APPROVAL_DEMO_OFF:
        return;
    case APPROVAL_DEMO_LOW:
    case APPROVAL_DEMO_HIGH:
    case APPROVAL_DEMO_CLICKED:
    case APPROVAL_DEMO_HOLDING:
    case APPROVAL_DEMO_HOLD:
    case APPROVAL_DEMO_NUDGE:
    case APPROVAL_DEMO_ANY:
    case APPROVAL_DEMO_INACTIVE:
        break;
    case APPROVAL_DEMO_MED:
        a.tool = "Edit";
        a.risk = PROTO_RISK_MED;
        a.words = "Widen the decision reply type";
        a.details = "packages/server/src/routes/\n  hooks.ts";
        a.prompt = false;
        a.count = 3;
        a.pages = 2;
        a.lines_after = 3;
        a.remaining_permille = 566;
        a.remaining_s = 17;
        break;
    case APPROVAL_DEMO_DETAILS:
        a.tool = "Edit";
        a.risk = PROTO_RISK_MED;
        a.words = "";
        a.details = "- decision: {behavior:\"allow\"}\n+ decision: AllowDecision\n+   | DenyDecision";
        a.prompt = false;
        a.cmd_line = "packages/server/src/routes/hooks.ts";
        a.count = 3;
        a.page = 2;
        a.pages = 2;
        a.remaining_permille = 400;
        a.remaining_s = 12;
        break;
    case APPROVAL_DEMO_LONG:
        a.risk = PROTO_RISK_MED;
        a.words = "Run the migration and restart the API";
        a.details = "  docker compose -f deploy/compose\n  .yml run --rm migrate --target\n  0042 && docker compose restart";
        a.pages = 2;
        a.lines_after = 2;
        a.allow = CARD_ALLOW_INACTIVE;
        a.remaining_permille = 700;
        a.remaining_s = 21;
        break;
    case APPROVAL_DEMO_SENT:
        a.sent = true;
        break;
    case APPROVAL_DEMO_NEXT:
        a.words = "Check the code style";
        a.details = "  pnpm run lint";
        a.position = 2;
        a.count = 3;
        a.remaining_permille = 1000;
        a.remaining_s = 30;
        a.armed = false;
        break;
    case APPROVAL_DEMO_MENU:
    case APPROVAL_DEMO_MENU_HIGH:
        a.view = CARD_VIEW_MENU;
        a.remaining_permille = 633;
        a.remaining_s = 19;
        break;
    case APPROVAL_DEMO_MENU_SESSION:
    case APPROVAL_DEMO_MENU_LOCKED:
        a.view = CARD_VIEW_MENU;
        a.cursor = 1;
        a.remaining_permille = 533;
        a.remaining_s = 16;
        if (demo == APPROVAL_DEMO_MENU_LOCKED) {
            a.session = CARD_SESSION_LOCKED;
            a.allow = CARD_ALLOW_INACTIVE;
        }
        break;
    }
    if (demo == APPROVAL_DEMO_MENU_HIGH) {
        a.cursor = 2;
    }
    apply(&a);
    switch ((ui_approval_demo_t)demo) {
    case APPROVAL_DEMO_HOLD:
    case APPROVAL_DEMO_HOLDING:
        ui_tabs_hold_progress(UI_TAB_APPROVE, 620);
        break;
    case APPROVAL_DEMO_NUDGE:
    case APPROVAL_DEMO_CLICKED:
        ui_tabs_nudge(UI_TAB_APPROVE, "Hold to allow");
        break;
    case APPROVAL_DEMO_ANY: {
        const ui_tabs_t any = {.any = "Any button: OK"};
        ui_screen_set_tabs(UI_SCREEN_APPROVAL, &any);
        break;
    }
    case APPROVAL_DEMO_INACTIVE:
        a.armed = false;
        apply(&a);
        break;
    case APPROVAL_DEMO_NEXT:
        w_toast_strip(UI_ICON_X, UI_C_CORAL, "Denied \xC2\xB7 next request", "", 0);
        break;
    case APPROVAL_DEMO_OFF:
    case APPROVAL_DEMO_LOW:
    case APPROVAL_DEMO_MED:
    case APPROVAL_DEMO_DETAILS:
    case APPROVAL_DEMO_LONG:
    case APPROVAL_DEMO_HIGH:
    case APPROVAL_DEMO_SENT:
    case APPROVAL_DEMO_MENU:
    case APPROVAL_DEMO_MENU_SESSION:
    case APPROVAL_DEMO_MENU_HIGH:
    case APPROVAL_DEMO_MENU_LOCKED:
        break;
    }
}
