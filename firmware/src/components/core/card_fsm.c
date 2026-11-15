#include "card_fsm.h"

#include "text.h"

#include <stdio.h>
#include <string.h>

static const card_out_t NONE = {.kind = CARD_OUT_NONE};

/* Tools whose cmd is a file change: the path, the line counts, then the change (protocol 14.2). */
static bool is_file_change(const char *tool)
{
    return strcmp(tool, "Edit") == 0 || strcmp(tool, "Write") == 0 || strcmp(tool, "MultiEdit") == 0 ||
           strcmp(tool, "NotebookEdit") == 0;
}

static uint8_t clamp_pages(size_t n)
{
    return n > UINT8_MAX ? UINT8_MAX : (uint8_t)n;
}

void card_fsm_open(card_fsm_t *c, const reqq_entry_t *e, bool decide)
{
    memset(c, 0, sizeof(*c));
    c->risk = e->risk;
    c->cut = e->cut;
    c->grant = e->has_grant && !e->cut;
    c->decide = decide;
    c->deadline_ms = e->deadline_ms;
    c->cols = e->has_cmd && strcmp(e->tool, "Bash") == 0 ? CARD_COLS - 2 : CARD_COLS;
    c->pages = clamp_pages(text_wrap_pages(e->cmd, c->cols, CARD_LINES));
    c->required = c->pages;
    if (e->has_cmd && is_file_change(e->tool) && c->pages > 0) {
        const size_t first = text_wrap_lines_first(e->cmd, c->cols);
        c->required = clamp_pages((first + CARD_LINES - 1) / CARD_LINES);
    }
    c->seen = c->pages > 0 ? 1 : 0;
}

static bool expired(const card_fsm_t *c, uint32_t now_ms)
{
    return (int32_t)(now_ms - c->deadline_ms) >= 0;
}

bool card_fsm_reading_done(const card_fsm_t *c)
{
    return !c->cut && c->required <= CARD_MAX_PAGES && c->seen >= c->required;
}

bool card_fsm_allow_enabled(const card_fsm_t *c, bool armed)
{
    return armed && card_fsm_reading_done(c);
}

card_allow_t card_fsm_allow(const card_fsm_t *c)
{
    if (c->cut || c->required > CARD_MAX_PAGES) {
        return CARD_ALLOW_NONE;
    }
    if (!card_fsm_reading_done(c)) {
        return CARD_ALLOW_INACTIVE;
    }
    return c->risk == PROTO_RISK_HIGH ? CARD_ALLOW_HOLD : CARD_ALLOW_CLICK;
}

card_session_t card_fsm_session(const card_fsm_t *c)
{
    if (!c->decide || !c->grant || c->cut || c->risk == PROTO_RISK_HIGH) {
        return CARD_SESSION_ABSENT;
    }
    return card_fsm_reading_done(c) ? CARD_SESSION_READY : CARD_SESSION_LOCKED;
}

uint8_t card_fsm_rows(const card_fsm_t *c, card_row_t rows[CARD_MENU_ROWS])
{
    uint8_t n = 0;
    rows[n++] = CARD_ROW_TERMINAL;
    if (!c->decide) {
        return n;
    }
    if (card_fsm_session(c) != CARD_SESSION_ABSENT) {
        rows[n++] = CARD_ROW_SESSION;
    }
    rows[n++] = CARD_ROW_DENY_ASK_WHY;
    rows[n++] = CARD_ROW_DENY_STOP;
    return n;
}

card_row_t card_fsm_focused_row(const card_fsm_t *c)
{
    card_row_t rows[CARD_MENU_ROWS];
    const uint8_t n = card_fsm_rows(c, rows);
    return rows[c->cursor < n ? c->cursor : 0];
}

btn_mode_t card_fsm_mode(const card_fsm_t *c, btn_id_t btn, uint32_t now_ms)
{
    if (c->sent || expired(c, now_ms)) {
        return BTN_MODE_NONE;
    }
    if (c->view == CARD_VIEW_MENU) {
        if (btn != BTN_APPROVE) {
            return BTN_MODE_CLICK;
        }
        if (card_fsm_focused_row(c) != CARD_ROW_SESSION) {
            return BTN_MODE_CLICK;
        }
        return card_fsm_session(c) == CARD_SESSION_READY ? BTN_MODE_COMMIT_HOLD : BTN_MODE_CLICK;
    }
    switch (btn) {
    case BTN_REJECT:
        return BTN_MODE_CLICK;
    case BTN_ACTION:
        return BTN_MODE_NAV_HOLD;
    case BTN_APPROVE:
        switch (card_fsm_allow(c)) {
        case CARD_ALLOW_NONE:
            return BTN_MODE_NONE;
        case CARD_ALLOW_INACTIVE:
        case CARD_ALLOW_CLICK:
            return BTN_MODE_CLICK;
        case CARD_ALLOW_HOLD:
            return BTN_MODE_COMMIT_HOLD;
        }
        break;
    case BTN_COUNT:
        break;
    }
    return BTN_MODE_NONE;
}

static card_out_t out_of(card_out_kind_t kind, btn_id_t btn)
{
    return (card_out_t){.kind = kind, .btn = btn};
}

static card_out_t decide(card_fsm_t *c, card_decision_t d, uint32_t now_ms)
{
    c->holding = false;
    if (!card_fsm_may_send(c, d, now_ms)) {
        return NONE;
    }
    return (card_out_t){.kind = CARD_OUT_DECIDE, .btn = BTN_COUNT, .decision = d};
}

/* PROGRESS, COMMIT and CANCELLED of a commit hold; granting is the caller's to check again. */
static card_out_t hold(card_fsm_t *c, const btn_fsm_out_t *out, bool may_grant, card_decision_t d, uint32_t now_ms)
{
    switch (out->kind) {
    case BTN_PROGRESS:
        if (out->permille == 0) {
            c->holding = may_grant;
        }
        if (!c->holding) {
            return NONE;
        }
        return (card_out_t){.kind = CARD_OUT_PROGRESS, .btn = out->id, .permille = out->permille};
    case BTN_COMMIT:
        if (!c->holding || !may_grant) {
            c->holding = false;
            return NONE;
        }
        return decide(c, d, now_ms);
    case BTN_CANCELLED:
        c->holding = false;
        return out_of(CARD_OUT_NUDGE, out->id);
    case BTN_CLICK:
    case BTN_HOLD:
        break;
    }
    return NONE;
}

static card_out_t on_card(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    switch (out->id) {
    case BTN_REJECT:
        return out->kind == BTN_CLICK ? decide(c, CARD_DECISION_DENY, now_ms) : NONE;
    case BTN_ACTION:
        if (out->kind == BTN_HOLD) {
            c->view = CARD_VIEW_MENU;
            c->cursor = 0;
            c->holding = false;
            return (card_out_t){.kind = CARD_OUT_MENU_OPEN, .btn = BTN_ACTION, .arm_ms = CARD_ARM_MS};
        }
        if (out->kind != BTN_CLICK) {
            return NONE;
        }
        if (c->pages < 2) {
            return out_of(CARD_OUT_NUDGE, BTN_ACTION);
        }
        c->page = (uint8_t)((c->page + 1) % c->pages);
        if (c->page + 1 > c->seen) {
            c->seen = (uint8_t)(c->page + 1);
        }
        return out_of(CARD_OUT_PAGE, BTN_ACTION);
    case BTN_APPROVE: {
        const card_allow_t allow = card_fsm_allow(c);
        if (out->kind == BTN_CLICK) {
            if (allow == CARD_ALLOW_CLICK) {
                return decide(c, CARD_DECISION_ALLOW, now_ms);
            }
            return allow == CARD_ALLOW_INACTIVE ? out_of(CARD_OUT_SHAKE, BTN_APPROVE) : NONE;
        }
        return hold(c, out, allow == CARD_ALLOW_HOLD, CARD_DECISION_ALLOW, now_ms);
    }
    case BTN_COUNT:
        break;
    }
    return NONE;
}

static card_decision_t row_decision(card_row_t row)
{
    switch (row) {
    case CARD_ROW_TERMINAL:
        return CARD_DECISION_DEFER;
    case CARD_ROW_SESSION:
        return CARD_DECISION_ALLOW_SESSION;
    case CARD_ROW_DENY_ASK_WHY:
        return CARD_DECISION_DENY_ASK_WHY;
    case CARD_ROW_DENY_STOP:
        return CARD_DECISION_DENY_STOP;
    }
    return CARD_DECISION_NONE;
}

static card_out_t on_menu(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    card_row_t rows[CARD_MENU_ROWS];
    const uint8_t n = card_fsm_rows(c, rows);
    switch (out->id) {
    case BTN_REJECT:
        if (out->kind != BTN_CLICK) {
            return NONE;
        }
        c->view = CARD_VIEW_CARD;
        c->holding = false;
        return (card_out_t){.kind = CARD_OUT_MENU_BACK, .btn = BTN_REJECT, .arm_ms = CARD_ARM_MS};
    case BTN_ACTION:
        if (out->kind != BTN_CLICK) {
            return NONE;
        }
        c->cursor = (uint8_t)((c->cursor + 1) % n);
        c->holding = false;
        return out_of(CARD_OUT_CURSOR, BTN_ACTION);
    case BTN_APPROVE: {
        const card_row_t row = card_fsm_focused_row(c);
        if (row != CARD_ROW_SESSION) {
            return out->kind == BTN_CLICK ? decide(c, row_decision(row), now_ms) : NONE;
        }
        const bool ready = card_fsm_session(c) == CARD_SESSION_READY;
        if (out->kind == BTN_CLICK) {
            return ready ? NONE : out_of(CARD_OUT_SHAKE, BTN_APPROVE);
        }
        return hold(c, out, ready, CARD_DECISION_ALLOW_SESSION, now_ms);
    }
    case BTN_COUNT:
        break;
    }
    return NONE;
}

card_out_t card_fsm_button(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (out->swallowed || c->sent) {
        return NONE;
    }
    if (expired(c, now_ms)) {
        return card_fsm_tick(c, now_ms);
    }
    return c->view == CARD_VIEW_MENU ? on_menu(c, out, now_ms) : on_card(c, out, now_ms);
}

card_out_t card_fsm_tick(card_fsm_t *c, uint32_t now_ms)
{
    if (c->holding && expired(c, now_ms)) {
        c->holding = false;
        return out_of(CARD_OUT_CANCEL, BTN_APPROVE);
    }
    return NONE;
}

bool card_fsm_may_send(const card_fsm_t *c, card_decision_t d, uint32_t now_ms)
{
    if (c->sent || expired(c, now_ms)) {
        return false;
    }
    switch (d) {
    case CARD_DECISION_NONE:
        return false;
    case CARD_DECISION_ALLOW:
        return card_fsm_allow(c) == CARD_ALLOW_CLICK || card_fsm_allow(c) == CARD_ALLOW_HOLD;
    case CARD_DECISION_DENY:
    case CARD_DECISION_DEFER:
        return true;
    case CARD_DECISION_ALLOW_SESSION:
        return card_fsm_session(c) == CARD_SESSION_READY;
    case CARD_DECISION_DENY_ASK_WHY:
    case CARD_DECISION_DENY_STOP:
        return c->decide;
    }
    return false;
}

void card_fsm_set_sent(card_fsm_t *c, bool sent)
{
    c->sent = sent;
    c->holding = false;
    if (sent) {
        c->view = CARD_VIEW_CARD;
    }
}

void card_fsm_cancel_hold(card_fsm_t *c)
{
    c->holding = false;
}

static void receipt(card_receipt_t *r, card_tone_t tone, const char *title, const char *detail)
{
    r->tone = tone;
    snprintf(r->title, sizeof(r->title), "%s", title);
    snprintf(r->detail, sizeof(r->detail), "%s", detail);
}

static void own_answer(card_receipt_t *r, const proto_dismiss_t *d, const reqq_entry_t *e)
{
    switch (d->choice) {
    case PROTO_CHOICE_ALLOW_SESSION:
        receipt(r, CARD_TONE_ALLOWED, "Allowed", "");
        snprintf(r->detail, sizeof(r->detail), "%s%s for this session", e->grant_rule,
                 e->grant_prefix ? " \xE2\x80\xA6" : "");
        return;
    case PROTO_CHOICE_DENY_ASK_WHY:
        receipt(r, CARD_TONE_DENIED, "Denied", "Claude will explain why");
        return;
    case PROTO_CHOICE_DENY_STOP:
        receipt(r, CARD_TONE_DENIED, "Denied", "Claude stopped");
        return;
    default:
        break;
    }
    switch (d->outcome) {
    case PROTO_OUTCOME_ALLOW:
        receipt(r, CARD_TONE_ALLOWED, "Allowed", "This call only");
        return;
    case PROTO_OUTCOME_DENY:
        receipt(r, CARD_TONE_DENIED, "Denied", "Claude was told to try another way");
        return;
    case PROTO_OUTCOME_NONE:
        break;
    }
    receipt(r, CARD_TONE_NEUTRAL, "Answered", "");
}

void card_receipt_dismiss(card_receipt_t *r, const proto_dismiss_t *d, bool ours, const reqq_entry_t *e)
{
    switch (d->reason) {
    case PROTO_DISMISS_ANSWERED:
        if (ours) {
            own_answer(r, d, e);
        } else if (d->by[0] == '\0') {
            receipt(r, CARD_TONE_NEUTRAL, "Answered elsewhere", "");
        } else if (strcmp(d->by, "dashboard") == 0) {
            receipt(r, CARD_TONE_NEUTRAL, "Answered on dashboard", "");
        } else {
            receipt(r, CARD_TONE_NEUTRAL, "Answered on ", "");
            strncat(r->title, d->by, sizeof(r->title) - strlen(r->title) - 1);
        }
        return;
    case PROTO_DISMISS_CANCELLED:
        receipt(r, CARD_TONE_NEUTRAL, "Answered in terminal", "");
        return;
    case PROTO_DISMISS_TIMEOUT:
        card_receipt_timeout(r);
        return;
    case PROTO_DISMISS_RELEASED:
        break;
    }
    receipt(r, CARD_TONE_NEUTRAL, "Moved to terminal", "");
}

void card_receipt_timeout(card_receipt_t *r)
{
    receipt(r, CARD_TONE_NEUTRAL, "Time ran out", "Sent to terminal");
}

const char *card_decision_name(card_decision_t d)
{
    switch (d) {
    case CARD_DECISION_NONE:
        return "none";
    case CARD_DECISION_ALLOW:
        return "allow";
    case CARD_DECISION_DENY:
        return "deny";
    case CARD_DECISION_DEFER:
        return "defer";
    case CARD_DECISION_ALLOW_SESSION:
        return "allow_session";
    case CARD_DECISION_DENY_ASK_WHY:
        return "deny_ask_why";
    case CARD_DECISION_DENY_STOP:
        return "deny_stop";
    }
    return "";
}

const char *card_session_name(card_session_t s)
{
    switch (s) {
    case CARD_SESSION_ABSENT:
        return "absent";
    case CARD_SESSION_LOCKED:
        return "locked";
    case CARD_SESSION_READY:
        return "ready";
    }
    return "";
}

const char *card_row_name(card_row_t r)
{
    switch (r) {
    case CARD_ROW_TERMINAL:
        return "terminal";
    case CARD_ROW_SESSION:
        return "session";
    case CARD_ROW_DENY_ASK_WHY:
        return "deny_ask_why";
    case CARD_ROW_DENY_STOP:
        return "deny_stop";
    }
    return "";
}
