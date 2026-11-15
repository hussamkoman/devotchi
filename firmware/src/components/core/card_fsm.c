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
    c->kind = REQQ_PERMISSION;
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

static uint8_t lowest_bit(uint8_t mask)
{
    for (uint8_t i = 0; i < 8; i++) {
        if ((mask & (1u << i)) != 0) {
            return i;
        }
    }
    return 0;
}

static bool q_answered(const card_fsm_t *c, uint8_t q)
{
    return (c->answered & (1u << q)) != 0;
}

/* Shows question q: its read page first when read is set and the text needs one, else its options. */
static void enter_question(card_fsm_t *c, uint8_t q, bool read)
{
    c->q = q;
    c->page = 0;
    c->seen = 1;
    c->pages = c->read_pages[q] > 0 ? c->read_pages[q] : 1;
    c->view = read && c->read_pages[q] > 0 ? CARD_VIEW_READ : CARD_VIEW_CARD;
    /* Back keeps the answer: the earlier choice (or the first tick) has the focus. */
    c->opt = c->mask[q] != 0 ? lowest_bit(c->mask[q]) : 0;
}

void card_fsm_open_ask(card_fsm_t *c, const reqq_entry_t *e, const uint16_t text_lines[PROTO_Q_MAX], uint32_t caps)
{
    memset(c, 0, sizeof(*c));
    c->kind = e->kind;
    c->deadline_ms = e->deadline_ms;
    c->can_extend = (caps & (e->kind == REQQ_PLAN ? PROTO_CAP_PLAN : PROTO_CAP_ASK)) != 0;
    if (e->kind == REQQ_PLAN) {
        const qstore_plan_t *p = &e->slot->u.plan;
        c->n_steps = p->n_steps;
        c->more = p->more;
        c->cut = p->cut || p->more > 0;
        c->pages = clamp_pages(p->n_steps > 0 ? (p->n_steps + CARD_PLAN_STEPS - 1) / CARD_PLAN_STEPS : 1);
        c->required = c->pages;
        c->seen = 1;
        return;
    }
    c->q_count = e->slot->count <= PROTO_Q_MAX ? e->slot->count : PROTO_Q_MAX;
    for (uint8_t i = 0; i < c->q_count; i++) {
        const qstore_question_t *q = &e->slot->u.q[i];
        c->n_options[i] = q->n_options <= PROTO_Q_OPTIONS_MAX ? q->n_options : PROTO_Q_OPTIONS_MAX;
        c->multi[i] = q->multi;
        const uint16_t lines = text_lines != NULL ? text_lines[i] : 0;
        c->read_pages[i] = lines > CARD_Q_LINES ? clamp_pages((lines + CARD_READ_LINES - 1u) / CARD_READ_LINES) : 0;
    }
    enter_question(c, 0, true);
}

void card_fsm_set_deadline(card_fsm_t *c, uint32_t deadline_ms)
{
    c->deadline_ms = deadline_ms;
}

uint8_t card_fsm_q_rows(const card_fsm_t *c)
{
    if (c->kind != REQQ_QUESTION || c->q >= c->q_count) {
        return 0;
    }
    return (uint8_t)(c->n_options[c->q] + (c->multi[c->q] ? 1 : 0));
}

size_t card_fsm_answers(const card_fsm_t *c, card_decision_t d, proto_answer_t out[PROTO_Q_MAX])
{
    if (c->kind != REQQ_QUESTION || (d != CARD_DECISION_ANSWER && d != CARD_DECISION_NO_PREFERENCE)) {
        return 0;
    }
    for (uint8_t i = 0; i < c->q_count; i++) {
        if (q_answered(c, i)) {
            out[i] = (proto_answer_t){.mask = c->mask[i]};
        } else if (d == CARD_DECISION_NO_PREFERENCE) {
            out[i] = (proto_answer_t){.no_preference = true};
        } else {
            return 0;
        }
    }
    return c->q_count;
}

bool card_fsm_reading_done(const card_fsm_t *c)
{
    return !c->cut && c->required <= CARD_MAX_PAGES && c->seen >= c->required;
}

bool card_fsm_allow_enabled(const card_fsm_t *c, bool armed)
{
    return armed && card_fsm_reading_done(c);
}

/* Approve on the device: no step left out, and few enough pages to read (protocol 14.5). */
static card_allow_t plan_approve(const card_fsm_t *c)
{
    if (c->more > 0 || c->pages > CARD_PLAN_MAX_PAGES) {
        return CARD_ALLOW_NONE;
    }
    if (c->seen < c->pages) {
        return CARD_ALLOW_INACTIVE;
    }
    return c->hold_only ? CARD_ALLOW_HOLD : CARD_ALLOW_CLICK;
}

/* "Approve, accept all edits" needs the whole plan on screen: only where Approve is offered and the frame is not cut. */
static card_session_t plan_edits(const card_fsm_t *c)
{
    const card_allow_t approve = plan_approve(c);
    if (approve == CARD_ALLOW_NONE || c->cut) {
        return CARD_SESSION_ABSENT;
    }
    return approve == CARD_ALLOW_INACTIVE ? CARD_SESSION_LOCKED : CARD_SESSION_READY;
}

card_allow_t card_fsm_allow(const card_fsm_t *c)
{
    if (c->kind == REQQ_PLAN) {
        return plan_approve(c);
    }
    if (c->kind == REQQ_QUESTION) {
        return CARD_ALLOW_NONE;
    }
    if (c->cut || c->required > CARD_MAX_PAGES) {
        return CARD_ALLOW_NONE;
    }
    if (!card_fsm_reading_done(c)) {
        return CARD_ALLOW_INACTIVE;
    }
    return c->risk == PROTO_RISK_HIGH || c->hold_only ? CARD_ALLOW_HOLD : CARD_ALLOW_CLICK;
}

card_session_t card_fsm_session(const card_fsm_t *c)
{
    if (c->kind == REQQ_PLAN) {
        return plan_edits(c);
    }
    if (c->kind == REQQ_QUESTION || !c->decide || !c->grant || c->cut || c->risk == PROTO_RISK_HIGH) {
        return CARD_SESSION_ABSENT;
    }
    return card_fsm_reading_done(c) ? CARD_SESSION_READY : CARD_SESSION_LOCKED;
}

uint8_t card_fsm_rows(const card_fsm_t *c, card_row_t rows[CARD_MENU_ROWS])
{
    uint8_t n = 0;
    rows[n++] = CARD_ROW_TERMINAL;
    if (c->kind == REQQ_QUESTION) {
        rows[n++] = CARD_ROW_READ_AGAIN;
        rows[n++] = CARD_ROW_LET_CLAUDE;
        rows[n++] = CARD_ROW_DECLINE;
        return n;
    }
    if (c->kind == REQQ_PLAN) {
        if (plan_edits(c) != CARD_SESSION_ABSENT) {
            rows[n++] = CARD_ROW_ACCEPT_EDITS;
        }
        rows[n++] = CARD_ROW_DENY_STOP;
        return n;
    }
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

/* ux-flows 2: the question's read page has no APPROVE; ACTION pages or opens the menu everywhere but in it. */
static btn_mode_t question_mode(const card_fsm_t *c, btn_id_t btn)
{
    if (c->view == CARD_VIEW_MENU || btn == BTN_REJECT) {
        return BTN_MODE_CLICK;
    }
    if (btn == BTN_ACTION) {
        return BTN_MODE_NAV_HOLD;
    }
    return c->view == CARD_VIEW_READ ? BTN_MODE_NONE : BTN_MODE_CLICK;
}

static btn_mode_t plan_mode(const card_fsm_t *c, btn_id_t btn)
{
    if (c->view == CARD_VIEW_MENU) {
        if (btn == BTN_APPROVE && card_fsm_focused_row(c) == CARD_ROW_ACCEPT_EDITS &&
            plan_edits(c) == CARD_SESSION_READY) {
            return BTN_MODE_COMMIT_HOLD;
        }
        return BTN_MODE_CLICK;
    }
    switch (btn) {
    case BTN_REJECT:
        return BTN_MODE_CLICK;
    case BTN_ACTION:
        return BTN_MODE_NAV_HOLD;
    case BTN_APPROVE:
        switch (plan_approve(c)) {
        case CARD_ALLOW_NONE:
            return BTN_MODE_NONE;
        case CARD_ALLOW_HOLD:
            return BTN_MODE_COMMIT_HOLD;
        case CARD_ALLOW_INACTIVE:
        case CARD_ALLOW_CLICK:
            break;
        }
        return BTN_MODE_CLICK;
    case BTN_COUNT:
        break;
    }
    return BTN_MODE_NONE;
}

btn_mode_t card_fsm_mode(card_fsm_t *c, btn_id_t btn, uint32_t now_ms)
{
    if (btn == BTN_APPROVE) {
        c->approve_moved = false;
    }
    if (c->sent || expired(c, now_ms)) {
        return BTN_MODE_NONE;
    }
    if (c->kind == REQQ_QUESTION) {
        return question_mode(c, btn);
    }
    if (c->kind == REQQ_PLAN) {
        return plan_mode(c, btn);
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
    case CARD_ROW_LET_CLAUDE:
        return CARD_DECISION_NO_PREFERENCE;
    case CARD_ROW_DECLINE:
        return CARD_DECISION_DECLINE;
    case CARD_ROW_ACCEPT_EDITS:
        return CARD_DECISION_APPROVE_EDITS;
    case CARD_ROW_READ_AGAIN:
        break;
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

/* A new view where APPROVE means something else: the buttons arm again. */
static card_out_t view_out(btn_id_t btn)
{
    return (card_out_t){.kind = CARD_OUT_VIEW, .btn = btn, .arm_ms = CARD_ARM_MS};
}

static card_out_t open_menu(card_fsm_t *c)
{
    c->back_view = c->view;
    c->view = CARD_VIEW_MENU;
    c->cursor = 0;
    c->holding = false;
    return (card_out_t){.kind = CARD_OUT_MENU_OPEN, .btn = BTN_ACTION, .arm_ms = CARD_ARM_MS};
}

/* REJECT on a question: the first goes to the terminal, a later one (or the review) back one question. */
static card_out_t q_back(card_fsm_t *c, uint32_t now_ms)
{
    if (c->view == CARD_VIEW_REVIEW) {
        enter_question(c, (uint8_t)(c->q_count - 1), false);
        return view_out(BTN_REJECT);
    }
    if (c->q == 0) {
        return decide(c, CARD_DECISION_DEFER, now_ms);
    }
    enter_question(c, (uint8_t)(c->q - 1), false);
    return view_out(BTN_REJECT);
}

/* The question on screen has its answer: the next question, the review, or with one question the answer goes out. */
static card_out_t q_advance(card_fsm_t *c, uint32_t now_ms)
{
    if (c->q_count == 1) {
        return decide(c, CARD_DECISION_ANSWER, now_ms);
    }
    if (c->q + 1 < c->q_count) {
        const uint8_t next = (uint8_t)(c->q + 1);
        /* A question answered before (and so read) opens on its options. */
        enter_question(c, next, !q_answered(c, next));
        return view_out(BTN_APPROVE);
    }
    c->view = CARD_VIEW_REVIEW;
    return view_out(BTN_APPROVE);
}

static card_out_t q_choose(card_fsm_t *c, uint32_t now_ms)
{
    const uint8_t n = c->n_options[c->q];
    if (!c->multi[c->q]) {
        if (c->opt >= n) {
            return NONE;
        }
        c->mask[c->q] = (uint8_t)(1u << c->opt);
    } else if (c->opt < n) {
        c->mask[c->q] ^= (uint8_t)(1u << c->opt);
        return out_of(CARD_OUT_TICK, BTN_APPROVE);
    }
    c->answered |= (uint8_t)(1u << c->q);
    return q_advance(c, now_ms);
}

static card_out_t on_question_menu(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    card_row_t rows[CARD_MENU_ROWS];
    const uint8_t n = card_fsm_rows(c, rows);
    switch (out->id) {
    case BTN_REJECT:
        c->view = c->back_view;
        return (card_out_t){.kind = CARD_OUT_MENU_BACK, .btn = BTN_REJECT, .arm_ms = CARD_ARM_MS};
    case BTN_ACTION:
        c->cursor = (uint8_t)((c->cursor + 1) % n);
        return out_of(CARD_OUT_CURSOR, BTN_ACTION);
    case BTN_APPROVE:
        switch (card_fsm_focused_row(c)) {
        case CARD_ROW_TERMINAL:
            return decide(c, CARD_DECISION_DEFER, now_ms);
        case CARD_ROW_READ_AGAIN:
            enter_question(c, c->q, false);
            c->view = CARD_VIEW_READ;
            return view_out(BTN_APPROVE);
        case CARD_ROW_LET_CLAUDE:
            return decide(c, CARD_DECISION_NO_PREFERENCE, now_ms);
        case CARD_ROW_DECLINE:
            return decide(c, CARD_DECISION_DECLINE, now_ms);
        default:
            break;
        }
        break;
    case BTN_COUNT:
        break;
    }
    return NONE;
}

static card_out_t on_question(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (out->kind == BTN_PROGRESS) {
        return NONE;
    }
    if (c->view != CARD_VIEW_MENU && out->id == BTN_ACTION && out->kind == BTN_HOLD) {
        return open_menu(c);
    }
    if (out->kind != BTN_CLICK) {
        return NONE;
    }
    switch (c->view) {
    case CARD_VIEW_MENU:
        return on_question_menu(c, out, now_ms);
    case CARD_VIEW_READ:
        if (out->id == BTN_REJECT) {
            return q_back(c, now_ms);
        }
        if (out->id != BTN_ACTION) {
            return NONE;
        }
        if (c->page + 1 < c->pages) {
            c->page++;
            c->seen = c->page + 1 > c->seen ? (uint8_t)(c->page + 1) : c->seen;
            return out_of(CARD_OUT_PAGE, BTN_ACTION);
        }
        c->view = CARD_VIEW_CARD;
        return view_out(BTN_ACTION);
    case CARD_VIEW_CARD:
        if (out->id == BTN_REJECT) {
            return q_back(c, now_ms);
        }
        if (out->id == BTN_ACTION) {
            c->opt = (uint8_t)((c->opt + 1) % card_fsm_q_rows(c));
            return out_of(CARD_OUT_CURSOR, BTN_ACTION);
        }
        return q_choose(c, now_ms);
    case CARD_VIEW_REVIEW:
        if (out->id == BTN_REJECT) {
            return q_back(c, now_ms);
        }
        return out->id == BTN_APPROVE ? decide(c, CARD_DECISION_ANSWER, now_ms) : out_of(CARD_OUT_NUDGE, BTN_ACTION);
    }
    return NONE;
}

static card_out_t on_plan_menu(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    card_row_t rows[CARD_MENU_ROWS];
    const uint8_t n = card_fsm_rows(c, rows);
    if (out->id == BTN_APPROVE) {
        const card_row_t row = card_fsm_focused_row(c);
        if (row != CARD_ROW_ACCEPT_EDITS) {
            return out->kind == BTN_CLICK ? decide(c, row_decision(row), now_ms) : NONE;
        }
        const bool ready = plan_edits(c) == CARD_SESSION_READY;
        if (out->kind == BTN_CLICK) {
            return ready ? NONE : out_of(CARD_OUT_SHAKE, BTN_APPROVE);
        }
        return hold(c, out, ready, CARD_DECISION_APPROVE_EDITS, now_ms);
    }
    if (out->kind != BTN_CLICK) {
        return NONE;
    }
    c->holding = false;
    if (out->id == BTN_REJECT) {
        c->view = CARD_VIEW_CARD;
        return (card_out_t){.kind = CARD_OUT_MENU_BACK, .btn = BTN_REJECT, .arm_ms = CARD_ARM_MS};
    }
    c->cursor = (uint8_t)((c->cursor + 1) % n);
    return out_of(CARD_OUT_CURSOR, BTN_ACTION);
}

static card_out_t on_plan(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (c->view == CARD_VIEW_MENU) {
        return on_plan_menu(c, out, now_ms);
    }
    if (out->id == BTN_ACTION && out->kind == BTN_HOLD) {
        return open_menu(c);
    }
    if (out->id == BTN_APPROVE && out->kind != BTN_CLICK) {
        return hold(c, out, plan_approve(c) == CARD_ALLOW_HOLD, CARD_DECISION_APPROVE, now_ms);
    }
    if (out->kind != BTN_CLICK) {
        return NONE;
    }
    switch (out->id) {
    case BTN_REJECT:
        return decide(c, CARD_DECISION_REVISE, now_ms);
    case BTN_ACTION:
        if (c->pages < 2) {
            return out_of(CARD_OUT_NUDGE, BTN_ACTION);
        }
        c->page = (uint8_t)((c->page + 1) % c->pages);
        if (c->page + 1 > c->seen) {
            c->seen = (uint8_t)(c->page + 1);
        }
        return out_of(CARD_OUT_PAGE, BTN_ACTION);
    case BTN_APPROVE:
        switch (plan_approve(c)) {
        case CARD_ALLOW_CLICK:
            return decide(c, CARD_DECISION_APPROVE, now_ms);
        case CARD_ALLOW_INACTIVE:
            return out_of(CARD_OUT_SHAKE, BTN_APPROVE);
        case CARD_ALLOW_HOLD:
            return out_of(CARD_OUT_NUDGE, BTN_APPROVE);
        default:
            return NONE;
        }
    case BTN_COUNT:
        break;
    }
    return NONE;
}

/* Every handled press on a question or plan keeps it alive (protocol 14.6), within its limits. */
static bool want_extend(card_fsm_t *c, uint32_t now_ms)
{
    if (c->kind == REQQ_PERMISSION || !c->can_extend) {
        return false;
    }
    if (c->extended && now_ms - c->extend_ms < CARD_EXTEND_GAP_MS) {
        return false;
    }
    if ((int32_t)(c->deadline_ms - now_ms) >= CARD_EXTEND_BELOW_MS) {
        return false;
    }
    c->extended = true;
    c->extend_ms = now_ms;
    return true;
}

card_out_t card_fsm_button(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (out->swallowed || c->sent) {
        return NONE;
    }
    if (expired(c, now_ms)) {
        return card_fsm_tick(c, now_ms);
    }
    /* The mode was latched for the row focused at press-down; the release must not decide another one. */
    if (out->id == BTN_APPROVE && c->approve_moved) {
        c->holding = false;
        return NONE;
    }
    card_out_t o;
    switch (c->kind) {
    case REQQ_QUESTION:
        o = on_question(c, out, now_ms);
        break;
    case REQQ_PLAN:
        o = on_plan(c, out, now_ms);
        break;
    case REQQ_PERMISSION:
    default:
        o = c->view == CARD_VIEW_MENU ? on_menu(c, out, now_ms) : on_card(c, out, now_ms);
        break;
    }
    if (o.kind == CARD_OUT_CURSOR) {
        c->approve_moved = true;
    }
    if (out->kind != BTN_PROGRESS && o.kind != CARD_OUT_DECIDE) {
        o.extend = want_extend(c, now_ms);
    }
    return o;
}

card_out_t card_fsm_tick(card_fsm_t *c, uint32_t now_ms)
{
    if (c->holding && expired(c, now_ms)) {
        c->holding = false;
        return out_of(CARD_OUT_CANCEL, BTN_APPROVE);
    }
    return NONE;
}

static bool question_may_send(const card_fsm_t *c, card_decision_t d)
{
    switch (d) {
    case CARD_DECISION_DEFER:
    case CARD_DECISION_NO_PREFERENCE:
    case CARD_DECISION_DECLINE:
        return true;
    case CARD_DECISION_ANSWER:
        /* Nothing goes out before the review card, unless the call has one question. */
        return c->q_count > 0 && c->answered == (uint8_t)((1u << c->q_count) - 1) &&
               (c->q_count == 1 || c->view == CARD_VIEW_REVIEW);
    default:
        return false;
    }
}

static bool plan_may_send(const card_fsm_t *c, card_decision_t d)
{
    switch (d) {
    case CARD_DECISION_DEFER:
    case CARD_DECISION_REVISE:
    case CARD_DECISION_DENY_STOP:
        return true;
    case CARD_DECISION_APPROVE:
        return plan_approve(c) == CARD_ALLOW_CLICK || plan_approve(c) == CARD_ALLOW_HOLD;
    case CARD_DECISION_APPROVE_EDITS:
        return !c->cut && plan_edits(c) == CARD_SESSION_READY;
    default:
        return false;
    }
}

bool card_fsm_may_send(const card_fsm_t *c, card_decision_t d, uint32_t now_ms)
{
    if (c->sent || expired(c, now_ms)) {
        return false;
    }
    if (c->kind == REQQ_QUESTION) {
        return question_may_send(c, d);
    }
    if (c->kind == REQQ_PLAN) {
        return plan_may_send(c, d);
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
    default:
        break;
    }
    return false;
}

void card_fsm_set_hold_only(card_fsm_t *c, bool on)
{
    c->hold_only = on;
}

void card_fsm_set_sent(card_fsm_t *c, bool sent)
{
    c->sent = sent;
    c->holding = false;
    if (sent && c->view == CARD_VIEW_MENU) {
        c->view = c->kind == REQQ_QUESTION ? c->back_view : CARD_VIEW_CARD;
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

/* Appends whole code points of src that fit. */
static void append(char *dst, size_t cap, const char *src)
{
    const size_t len = strlen(dst);
    if (len + 1 < cap) {
        text_utf8_truncate(dst + len, cap - len, src);
    }
}

void card_answer_text(char *dst, size_t cap, const qstore_question_t *q, const proto_answer_t *a)
{
    if (cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (a->no_preference) {
        append(dst, cap, "No preference");
        return;
    }
    if (a->mask == 0) {
        append(dst, cap, "None of these");
        return;
    }
    bool first = true;
    for (uint8_t i = 0; i < q->n_options && i < PROTO_Q_OPTIONS_MAX; i++) {
        if ((a->mask & (1u << i)) != 0) {
            append(dst, cap, first ? "" : ", ");
            append(dst, cap, q->options[i].label);
            first = false;
        }
    }
}

/* "Storage: Postgres · Auth: Bearer token": each answer after its header, when the question has one. */
static void answers_detail(card_receipt_t *r, const reqq_entry_t *e)
{
    r->detail[0] = '\0';
    char answer[PROTO_Q_OPTIONS_MAX * PROTO_Q_LABEL_SIZE];
    for (uint8_t i = 0; e->slot != NULL && i < e->n_answers && i < PROTO_Q_MAX; i++) {
        const qstore_question_t *q = &e->slot->u.q[i];
        append(r->detail, sizeof(r->detail), i > 0 ? " \xC2\xB7 " : "");
        if (q->has_header) {
            append(r->detail, sizeof(r->detail), q->header);
            append(r->detail, sizeof(r->detail), ": ");
        }
        card_answer_text(answer, sizeof(answer), q, &e->answers[i]);
        append(r->detail, sizeof(r->detail), answer);
    }
}

/* This device's answer to a question or plan was taken. */
static bool own_ask_answer(card_receipt_t *r, const proto_dismiss_t *d, const reqq_entry_t *e)
{
    switch (d->choice) {
    case PROTO_CHOICE_APPROVE:
        receipt(r, CARD_TONE_ALLOWED, "Plan approved", "you approve edits");
        return true;
    case PROTO_CHOICE_APPROVE_ACCEPT_EDITS:
        receipt(r, CARD_TONE_ALLOWED, "Plan approved", "edits accepted");
        return true;
    case PROTO_CHOICE_REVISE:
        receipt(r, CARD_TONE_NEUTRAL, "Sent back for changes", "");
        return true;
    case PROTO_CHOICE_DECLINE:
        receipt(r, CARD_TONE_DENIED, "Declined", "");
        return true;
    case PROTO_CHOICE_ANSWERED:
        break;
    default:
        if (e->kind != REQQ_QUESTION || d->outcome != PROTO_OUTCOME_ALLOW) {
            return false;
        }
        break;
    }
    receipt(r, CARD_TONE_ALLOWED, "Answers sent", "");
    answers_detail(r, e);
    return true;
}

static void own_answer(card_receipt_t *r, const proto_dismiss_t *d, const reqq_entry_t *e)
{
    if (e->kind != REQQ_PERMISSION && own_ask_answer(r, d, e)) {
        return;
    }
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
    case CARD_DECISION_ANSWER:
        return "answer";
    case CARD_DECISION_NO_PREFERENCE:
        return "no_preference";
    case CARD_DECISION_DECLINE:
        return "decline";
    case CARD_DECISION_APPROVE:
        return "approve";
    case CARD_DECISION_APPROVE_EDITS:
        return "approve_accept_edits";
    case CARD_DECISION_REVISE:
        return "revise";
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
    case CARD_ROW_READ_AGAIN:
        return "read_again";
    case CARD_ROW_LET_CLAUDE:
        return "let_claude";
    case CARD_ROW_DECLINE:
        return "decline";
    case CARD_ROW_ACCEPT_EDITS:
        return "accept_edits";
    }
    return "";
}

const char *card_view_name(card_view_t v)
{
    switch (v) {
    case CARD_VIEW_CARD:
        return "card";
    case CARD_VIEW_MENU:
        return "menu";
    case CARD_VIEW_READ:
        return "read";
    case CARD_VIEW_REVIEW:
        return "review";
    }
    return "";
}
