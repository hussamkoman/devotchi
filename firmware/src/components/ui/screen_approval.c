#include "ui_priv.h"

#include "text.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Layout of the mockups (20-24): chip row, code well, Claude's words, project and time left. */
#define CHIP_Y        25
#define QUEUE_W       60
#define WELL_Y        51
#define SUMMARY_GAP   8
#define SUMMARY_LINE  22
#define FOOT_Y        185
#define BAR_Y         208
#define SENT_Y        220
#define PROJECT_W     200

static w_chip_t *s_chip;
static lv_obj_t *s_queue;
static w_well_t *s_well;
static lv_obj_t *s_summary;
static lv_obj_t *s_project;
static w_countdown_t *s_bar;
static lv_obj_t *s_sent;

/* Guarded by the ui lock. */
static bool s_valid;
static proto_risk_t s_risk;
static bool s_sent_shown;
static bool s_paged;
static int32_t s_summary_y;
static uint8_t s_summary_lines;

lv_obj_t *screen_approval_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_chip = w_chip_create(scr, UI_PAD, CHIP_Y);
    s_queue = ui_text(scr, UI_FONT_SMALL, UI_C_FOG, LV_HOR_RES - UI_PAD - QUEUE_W, CHIP_Y + 2, QUEUE_W, true);
    lv_obj_set_style_text_align(s_queue, LV_TEXT_ALIGN_RIGHT, 0);
    s_well = w_well_create(scr, WELL_Y);
    /* Recolouring marks "Claude:" as the label of Claude's own words. */
    s_summary = ui_text(scr, UI_FONT_BODY, UI_C_FOG, UI_TEXT_X, WELL_Y, UI_WIDTH_TEXT, false);
    lv_label_set_long_mode(s_summary, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_recolor(s_summary, true);
    s_project = ui_text(scr, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, FOOT_Y, PROJECT_W, true);
    s_bar = w_countdown_create(scr, BAR_Y, true);
    s_sent = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, SENT_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(s_sent, "Sent\xE2\x80\xA6");
    lv_obj_add_flag(s_sent, LV_OBJ_FLAG_HIDDEN);
    s_valid = false;
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

static void apply_tabs(const ui_approval_t *a)
{
    const bool paged = a->pages > 1;
    if (s_valid && a->risk == s_risk && a->sent == s_sent_shown && paged == s_paged) {
        return;
    }
    ui_tabs_t tabs = {0};
    if (!a->sent) {
        tabs.tab[UI_TAB_REJECT] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Deny"};
        /* ACTION hold hands the request to the terminal prompt. */
        tabs.tab[UI_TAB_ACTION] = paged ? (ui_tab_t){.state = UI_TAB_CLICK_AND_HOLD, .label = "Page", .hold = "Terminal"}
                                        : (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "Terminal"};
        tabs.tab[UI_TAB_APPROVE] = a->risk == PROTO_RISK_HIGH
                                       ? (ui_tab_t){.state = UI_TAB_HOLD_ONLY, .hold = "Allow", .holding = "Allowing\xE2\x80\xA6"}
                                       : (ui_tab_t){.state = UI_TAB_CLICK, .label = "Allow"};
    }
    ui_screen_set_tabs(UI_SCREEN_APPROVAL, &tabs);
    ui_set_hidden(s_sent, !a->sent);
    s_paged = paged;
}

/* `Claude: "summary"` with the label in slate; '#' starts a recolour command, so it is doubled. */
static void summary_text(char *dst, size_t cap, const char *summary)
{
    static char clean[UI_LABEL_TEXT_SIZE];
    text_sanitize_device(clean, sizeof(clean), summary);
    size_t n = (size_t)snprintf(dst, cap, UI_RECOLOR_SLATE " Claude:# \xE2\x80\x9C");
    for (const char *p = clean; *p != '\0' && n + 5 < cap; p++) {
        if (*p == '#') {
            dst[n++] = '#';
        }
        dst[n++] = *p;
    }
    strlcpy(dst + n, "\xE2\x80\x9D", cap - n);
}

static void apply(const ui_approval_t *a)
{
    const lv_color_t color = risk_color(a->risk);
    const bool high = a->risk == PROTO_RISK_HIGH;
    w_chip_set(s_chip, tool_icon(a->tool), a->tool, color, (uint8_t)(a->risk + 1), risk_words(a->risk));
    apply_tabs(a);
    s_risk = a->risk;
    s_sent_shown = a->sent;
    s_valid = true;

    char line[48] = "";
    if (a->count > 1) {
        snprintf(line, sizeof(line), "%u/%u", a->position, a->count);
    }
    ui_label_set(s_queue, line);

    const size_t lines = a->pages > 0 ? text_wrap_lines(a->details, UI_DETAILS_COLS) : 0;
    const uint8_t shown = (uint8_t)(lines > UI_DETAILS_LINES ? UI_DETAILS_LINES : lines);
    char more[32] = "";
    char page[16] = "";
    const bool paged = a->pages > 1;
    if (paged) {
        if (a->lines_after > 0) {
            snprintf(more, sizeof(more), "+%u more line%s", a->lines_after, a->lines_after == 1 ? "" : "s");
        }
        snprintf(page, sizeof(page), "%u/%u", a->page, a->pages);
    }
    ui_set_hidden(w_well_obj(s_well), a->pages == 0);
    const int32_t well_h = a->pages > 0 ? w_well_set(s_well, a->details, shown, paged ? more : NULL,
                                                     paged ? page : NULL, high, color)
                                        : 0;

    /* Claude's words take two lines when they fit above the project line, else one. */
    const int32_t y = a->pages > 0 ? WELL_Y + well_h + SUMMARY_GAP : WELL_Y;
    const uint8_t summary_lines = y + 2 * SUMMARY_LINE <= FOOT_Y ? 2 : 1;
    if (y != s_summary_y || summary_lines != s_summary_lines) {
        lv_obj_set_y(s_summary, y);
        lv_obj_set_height(s_summary, summary_lines * SUMMARY_LINE);
        s_summary_y = y;
        s_summary_lines = summary_lines;
    }
    static char summary[UI_LABEL_TEXT_SIZE];
    summary_text(summary, sizeof(summary), a->summary);
    ui_set_hidden(s_summary, a->summary == NULL || a->summary[0] == '\0');
    ui_label_set(s_summary, summary);

    ui_label_set(s_project, a->project);
    w_countdown_set(s_bar, a->remaining_permille, a->remaining_s, color);
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
        .summary = "Run the server test suite",
        .details = "$ pnpm test --filter server",
        .position = 1,
        .count = 1,
        .page = 1,
        .pages = 1,
        .remaining_permille = 800,
        .remaining_s = 24,
    };
    ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
    switch ((ui_approval_demo_t)demo) {
    case APPROVAL_DEMO_OFF:
        return;
    case APPROVAL_DEMO_LOW:
        break;
    case APPROVAL_DEMO_MED:
        a.tool = "Edit";
        a.risk = PROTO_RISK_MED;
        a.summary = "Widen the decision reply type";
        a.details = "packages/server/src/routes/\n  hooks.ts";
        a.count = 3;
        a.pages = 2;
        a.lines_after = 3;
        a.remaining_permille = 566;
        a.remaining_s = 17;
        break;
    case APPROVAL_DEMO_DETAILS:
        a.tool = "Edit";
        a.risk = PROTO_RISK_MED;
        a.summary = "";
        a.details = "- decision: {behavior:\"allow\"}\n+ decision: AllowDecision\n+   | DenyDecision";
        a.count = 3;
        a.page = 2;
        a.pages = 2;
        a.remaining_permille = 400;
        a.remaining_s = 12;
        break;
    case APPROVAL_DEMO_SENT:
        a.sent = true;
        break;
    case APPROVAL_DEMO_HIGH:
    case APPROVAL_DEMO_HOLD:
    case APPROVAL_DEMO_NUDGE:
    case APPROVAL_DEMO_ANY:
    case APPROVAL_DEMO_INACTIVE:
        a.risk = PROTO_RISK_HIGH;
        a.summary = "Update the remote branch";
        a.details = "$ git push --force origin master";
        a.remaining_permille = 900;
        a.remaining_s = 27;
        break;
    }
    apply(&a);
    switch ((ui_approval_demo_t)demo) {
    case APPROVAL_DEMO_HOLD:
        ui_tabs_hold_progress(UI_TAB_APPROVE, 620);
        break;
    case APPROVAL_DEMO_NUDGE:
        ui_tabs_nudge(UI_TAB_APPROVE, "Hold to allow");
        break;
    case APPROVAL_DEMO_ANY: {
        const ui_tabs_t any = {.any = "Any button: OK"};
        ui_screen_set_tabs(UI_SCREEN_APPROVAL, &any);
        s_valid = false;
        break;
    }
    case APPROVAL_DEMO_INACTIVE: {
        const ui_tabs_t off = {.tab = {
                                   [UI_TAB_REJECT] = {.state = UI_TAB_INACTIVE, .label = "Deny"},
                                   [UI_TAB_ACTION] = {.state = UI_TAB_INACTIVE, .label = "Page"},
                                   [UI_TAB_APPROVE] = {.state = UI_TAB_INACTIVE, .label = "Allow"},
                               }};
        ui_screen_set_tabs(UI_SCREEN_APPROVAL, &off);
        s_valid = false;
        break;
    }
    case APPROVAL_DEMO_OFF:
    case APPROVAL_DEMO_LOW:
    case APPROVAL_DEMO_MED:
    case APPROVAL_DEMO_DETAILS:
    case APPROVAL_DEMO_HIGH:
    case APPROVAL_DEMO_SENT:
        break;
    }
}
