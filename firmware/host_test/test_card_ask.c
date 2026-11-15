/* card_fsm for questions and plans: the question and plan rows of ux-flows 2 and the table of protocol 14.5. */
#include "card_fsm.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define NOW       1000u
/* Forty seconds left: within the extend window. */
#define DEADLINE  (NOW + 40000u)

static qstore_slot_t s_slot;
static reqq_entry_t s_e;
static card_fsm_t s_c;
static uint32_t s_now = NOW;
static uint16_t s_lines[PROTO_Q_MAX];

static void base_entry(reqq_kind_t kind, qstore_kind_t slot_kind)
{
    memset(&s_slot, 0, sizeof(s_slot));
    memset(&s_e, 0, sizeof(s_e));
    memset(s_lines, 0, sizeof(s_lines));
    s_slot.kind = slot_kind;
    snprintf(s_slot.id, sizeof(s_slot.id), "%032x", 7);
    s_e.kind = kind;
    s_e.slot = &s_slot;
    memcpy(s_e.id, s_slot.id, sizeof(s_e.id));
    s_e.deadline_ms = DEADLINE;
    s_e.timeout_ms = 40000;
    s_now = NOW;
}

/* count questions of n options each; multi marks which are multi-select (bit per question). */
static void questions(uint8_t count, uint8_t n, uint8_t multi)
{
    base_entry(REQQ_QUESTION, QSTORE_QUESTION);
    s_slot.count = count;
    s_slot.complete = true;
    for (uint8_t i = 0; i < count; i++) {
        qstore_question_t *q = &s_slot.u.q[i];
        q->has_header = true;
        snprintf(q->header, sizeof(q->header), "Head %u", i);
        snprintf(q->text, sizeof(q->text), "Question %u?", i);
        q->multi = (multi & (1u << i)) != 0;
        q->n_options = n;
        for (uint8_t o = 0; o < n; o++) {
            snprintf(q->options[o].label, sizeof(q->options[o].label), "Opt %u.%u", i, o);
        }
        s_lines[i] = 1;
    }
}

static void plan(uint8_t steps, uint16_t more, bool cut)
{
    base_entry(REQQ_PLAN, QSTORE_PLAN);
    s_slot.count = 1;
    s_slot.complete = true;
    snprintf(s_slot.u.plan.title, sizeof(s_slot.u.plan.title), "Add pet packs");
    s_slot.u.plan.n_steps = steps;
    s_slot.u.plan.more = more;
    s_slot.u.plan.cut = cut;
}

static void open_ask(uint32_t caps)
{
    card_fsm_open_ask(&s_c, &s_e, s_lines, caps);
}

/* A click is a whole press: the app asks the mode at press-down. */
static card_out_t press(btn_id_t id, btn_kind_t kind)
{
    if (kind == BTN_CLICK) {
        card_fsm_mode(&s_c, id, s_now);
    }
    const btn_fsm_out_t out = {.id = id, .kind = kind};
    return card_fsm_button(&s_c, &out, s_now);
}

static card_out_t click(btn_id_t id)
{
    return press(id, BTN_CLICK);
}

static card_out_t commit_hold(void)
{
    const btn_fsm_out_t p0 = {.id = BTN_APPROVE, .kind = BTN_PROGRESS, .permille = 0};
    const btn_fsm_out_t p1 = {.id = BTN_APPROVE, .kind = BTN_PROGRESS, .permille = 500};
    card_fsm_mode(&s_c, BTN_APPROVE, s_now);
    card_fsm_button(&s_c, &p0, s_now);
    card_fsm_button(&s_c, &p1, s_now);
    return press(BTN_APPROVE, BTN_COMMIT);
}

static void expect_decision(card_out_t o, card_decision_t d)
{
    TEST_ASSERT_EQUAL_STRING(card_decision_name(d), o.kind == CARD_OUT_DECIDE ? card_decision_name(o.decision) : "-");
    TEST_ASSERT_TRUE(card_fsm_may_send(&s_c, d, s_now));
}

static void expect_view(card_out_t o, card_view_t v)
{
    TEST_ASSERT_EQUAL_STRING(card_view_name(v), card_view_name(s_c.view));
    if (o.kind == CARD_OUT_VIEW) {
        TEST_ASSERT_EQUAL_UINT16(CARD_ARM_MS, o.arm_ms);
    }
}

static void menu_to(card_row_t row)
{
    TEST_ASSERT_EQUAL(CARD_OUT_MENU_OPEN, press(BTN_ACTION, BTN_HOLD).kind);
    for (int i = 0; i < CARD_MENU_ROWS && card_fsm_focused_row(&s_c) != row; i++) {
        click(BTN_ACTION);
    }
    TEST_ASSERT_EQUAL_STRING(card_row_name(row), card_row_name(card_fsm_focused_row(&s_c)));
}

/* ---- questions ---- */

static void test_single_question_sends_at_once(void)
{
    questions(1, 3, 0);
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_EQUAL(CARD_VIEW_CARD, s_c.view);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(BTN_MODE_NAV_HOLD, card_fsm_mode(&s_c, BTN_ACTION, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ANSWER, s_now));
    /* The press that woke the screen or fell into the arming delay is swallowed. */
    const btn_fsm_out_t swallowed = {.id = BTN_APPROVE, .kind = BTN_CLICK, .swallowed = true};
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &swallowed, s_now).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL_UINT8(0, s_c.opt);
    click(BTN_ACTION);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);
    proto_answer_t a[PROTO_Q_MAX];
    TEST_ASSERT_EQUAL_size_t(1, card_fsm_answers(&s_c, CARD_DECISION_ANSWER, a));
    TEST_ASSERT_FALSE(a[0].no_preference);
    TEST_ASSERT_EQUAL_HEX8(1u << 1, a[0].mask);
    card_fsm_set_sent(&s_c, true);
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, click(BTN_APPROVE).kind);
}

static void test_reject_on_first_question_is_terminal(void)
{
    questions(2, 2, 0);
    open_ask(PROTO_CAP_ASK);
    expect_decision(click(BTN_REJECT), CARD_DECISION_DEFER);
}

static void test_long_question_opens_on_read_page(void)
{
    questions(1, 4, 0);
    s_lines[0] = 9;
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_EQUAL(CARD_VIEW_READ, s_c.view);
    TEST_ASSERT_EQUAL_UINT8(2, s_c.pages);
    /* Read page: no APPROVE tab, ACTION pages then shows the options. */
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, click(BTN_APPROVE).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_PAGE, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.page);
    const card_out_t o = click(BTN_ACTION);
    TEST_ASSERT_EQUAL(CARD_OUT_VIEW, o.kind);
    expect_view(o, CARD_VIEW_CARD);
    /* The menu reads it again, from the first page. */
    menu_to(CARD_ROW_READ_AGAIN);
    expect_view(click(BTN_APPROVE), CARD_VIEW_READ);
    TEST_ASSERT_EQUAL_UINT8(0, s_c.page);
    /* REJECT on the read page of the first question: terminal. */
    expect_decision(click(BTN_REJECT), CARD_DECISION_DEFER);
}

static void test_two_lines_need_no_read_page(void)
{
    questions(1, 2, 0);
    s_lines[0] = CARD_Q_LINES;
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_EQUAL(CARD_VIEW_CARD, s_c.view);
    /* "Read the question again" still shows the read page for a short text. */
    menu_to(CARD_ROW_READ_AGAIN);
    click(BTN_APPROVE);
    TEST_ASSERT_EQUAL(CARD_VIEW_READ, s_c.view);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.pages);
    expect_view(click(BTN_ACTION), CARD_VIEW_CARD);
}

static void test_two_questions_review_and_back(void)
{
    questions(2, 3, 0);
    open_ask(PROTO_CAP_ASK);
    click(BTN_ACTION);
    expect_view(click(BTN_APPROVE), CARD_VIEW_CARD);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.q);
    TEST_ASSERT_EQUAL_UINT8(0, s_c.opt);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ANSWER, s_now));
    click(BTN_ACTION);
    click(BTN_ACTION);
    expect_view(click(BTN_APPROVE), CARD_VIEW_REVIEW);
    /* All answered, but only the review's APPROVE sends. */
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, click(BTN_ACTION).kind);
    /* Back keeps every answer and focuses the earlier choice. */
    expect_view(click(BTN_REJECT), CARD_VIEW_CARD);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.q);
    TEST_ASSERT_EQUAL_UINT8(2, s_c.opt);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ANSWER, s_now));
    expect_view(click(BTN_REJECT), CARD_VIEW_CARD);
    TEST_ASSERT_EQUAL_UINT8(0, s_c.q);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.opt);
    TEST_ASSERT_EQUAL_HEX8(0x3, s_c.answered);
    /* Change the first answer, then the second keeps its choice. */
    click(BTN_ACTION);
    click(BTN_APPROVE);
    TEST_ASSERT_EQUAL_UINT8(2, s_c.opt);
    click(BTN_APPROVE);
    TEST_ASSERT_EQUAL(CARD_VIEW_REVIEW, s_c.view);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);
    proto_answer_t a[PROTO_Q_MAX];
    TEST_ASSERT_EQUAL_size_t(2, card_fsm_answers(&s_c, CARD_DECISION_ANSWER, a));
    TEST_ASSERT_EQUAL_HEX8(1u << 2, a[0].mask);
    TEST_ASSERT_EQUAL_HEX8(1u << 2, a[1].mask);
}

static void test_four_questions_answers_in_order(void)
{
    questions(4, 4, 0);
    s_lines[2] = 7;
    open_ask(PROTO_CAP_ASK);
    for (uint8_t q = 0; q < 4; q++) {
        TEST_ASSERT_EQUAL_UINT8(q, s_c.q);
        if (s_c.view == CARD_VIEW_READ) {
            TEST_ASSERT_EQUAL_UINT8(2, q);
            click(BTN_ACTION);
            click(BTN_ACTION);
        }
        for (uint8_t i = 0; i < q; i++) {
            click(BTN_ACTION);
        }
        TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ANSWER, s_now));
        click(BTN_APPROVE);
    }
    TEST_ASSERT_EQUAL(CARD_VIEW_REVIEW, s_c.view);
    /* Back to the third question (read before): it opens on its options. */
    click(BTN_REJECT);
    click(BTN_REJECT);
    TEST_ASSERT_EQUAL_UINT8(2, s_c.q);
    TEST_ASSERT_EQUAL(CARD_VIEW_CARD, s_c.view);
    click(BTN_APPROVE);
    click(BTN_APPROVE);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);
    proto_answer_t a[PROTO_Q_MAX];
    TEST_ASSERT_EQUAL_size_t(4, card_fsm_answers(&s_c, CARD_DECISION_ANSWER, a));
    for (uint8_t q = 0; q < 4; q++) {
        TEST_ASSERT_EQUAL_HEX8(1u << q, a[q].mask);
    }
}

static void test_multi_select(void)
{
    questions(1, 3, 0x1);
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_EQUAL_UINT8(4, card_fsm_q_rows(&s_c));
    TEST_ASSERT_EQUAL(CARD_OUT_TICK, click(BTN_APPROVE).kind);
    click(BTN_ACTION);
    click(BTN_ACTION);
    TEST_ASSERT_EQUAL(CARD_OUT_TICK, click(BTN_APPROVE).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_TICK, click(BTN_APPROVE).kind);
    TEST_ASSERT_EQUAL_HEX8(0x1, s_c.mask[0]);
    TEST_ASSERT_EQUAL(CARD_OUT_TICK, click(BTN_APPROVE).kind);
    TEST_ASSERT_EQUAL_HEX8(0x5, s_c.mask[0]);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ANSWER, s_now));
    click(BTN_ACTION);
    TEST_ASSERT_EQUAL_UINT8(3, s_c.opt);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);
    proto_answer_t a[PROTO_Q_MAX];
    card_fsm_answers(&s_c, CARD_DECISION_ANSWER, a);
    TEST_ASSERT_EQUAL_HEX8(0x5, a[0].mask);
}

static void test_multi_select_none_and_back(void)
{
    questions(2, 2, 0x2);
    open_ask(PROTO_CAP_ASK);
    click(BTN_APPROVE);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.q);
    TEST_ASSERT_EQUAL_UINT8(3, card_fsm_q_rows(&s_c));
    click(BTN_APPROVE);
    click(BTN_REJECT);
    TEST_ASSERT_EQUAL_UINT8(0, s_c.q);
    click(BTN_APPROVE);
    /* The tick stays ticked and has the focus. */
    TEST_ASSERT_EQUAL_HEX8(0x1, s_c.mask[1]);
    TEST_ASSERT_EQUAL_UINT8(0, s_c.opt);
    click(BTN_APPROVE);
    click(BTN_ACTION);
    click(BTN_ACTION);
    click(BTN_APPROVE);
    TEST_ASSERT_EQUAL(CARD_VIEW_REVIEW, s_c.view);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);
    proto_answer_t a[PROTO_Q_MAX];
    TEST_ASSERT_EQUAL_size_t(2, card_fsm_answers(&s_c, CARD_DECISION_ANSWER, a));
    TEST_ASSERT_EQUAL_HEX8(0x1, a[0].mask);
    TEST_ASSERT_EQUAL_HEX8(0x0, a[1].mask);
    TEST_ASSERT_FALSE(a[1].no_preference);
}

static void test_question_menu(void)
{
    questions(3, 2, 0);
    open_ask(PROTO_CAP_ASK);
    card_row_t rows[CARD_MENU_ROWS];
    TEST_ASSERT_EQUAL_UINT8(4, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_EQUAL(CARD_ROW_TERMINAL, rows[0]);
    TEST_ASSERT_EQUAL(CARD_ROW_READ_AGAIN, rows[1]);
    TEST_ASSERT_EQUAL(CARD_ROW_LET_CLAUDE, rows[2]);
    TEST_ASSERT_EQUAL(CARD_ROW_DECLINE, rows[3]);
    click(BTN_APPROVE);
    menu_to(CARD_ROW_TERMINAL);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    /* Back returns to the question it was opened from. */
    TEST_ASSERT_EQUAL(CARD_OUT_MENU_BACK, click(BTN_REJECT).kind);
    TEST_ASSERT_EQUAL(CARD_VIEW_CARD, s_c.view);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.q);
    menu_to(CARD_ROW_LET_CLAUDE);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_NO_PREFERENCE);
    proto_answer_t a[PROTO_Q_MAX];
    TEST_ASSERT_EQUAL_size_t(3, card_fsm_answers(&s_c, CARD_DECISION_NO_PREFERENCE, a));
    TEST_ASSERT_FALSE(a[0].no_preference);
    TEST_ASSERT_EQUAL_HEX8(0x1, a[0].mask);
    TEST_ASSERT_TRUE(a[1].no_preference);
    TEST_ASSERT_TRUE(a[2].no_preference);
    TEST_ASSERT_EQUAL_size_t(0, card_fsm_answers(&s_c, CARD_DECISION_ANSWER, a));
    TEST_ASSERT_EQUAL_size_t(0, card_fsm_answers(&s_c, CARD_DECISION_DECLINE, a));

    questions(2, 2, 0);
    open_ask(PROTO_CAP_ASK);
    menu_to(CARD_ROW_DECLINE);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_DECLINE);
    open_ask(PROTO_CAP_ASK);
    menu_to(CARD_ROW_TERMINAL);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_DEFER);
}

static void test_review_menu(void)
{
    questions(2, 2, 0);
    open_ask(PROTO_CAP_ASK);
    click(BTN_APPROVE);
    click(BTN_APPROVE);
    TEST_ASSERT_EQUAL(CARD_VIEW_REVIEW, s_c.view);
    TEST_ASSERT_EQUAL(BTN_MODE_NAV_HOLD, card_fsm_mode(&s_c, BTN_ACTION, s_now));
    menu_to(CARD_ROW_TERMINAL);
    click(BTN_REJECT);
    TEST_ASSERT_EQUAL(CARD_VIEW_REVIEW, s_c.view);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);
}

static void test_question_refuses_permission_decisions(void)
{
    questions(1, 2, 0);
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DENY, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_APPROVE, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DENY_STOP, s_now));
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
}

/* ---- plans: protocol 14.5, each frame against approve, accept edits and revise ---- */

static void test_plan_hold_only(void)
{
    plan(3, 0, false);
    open_ask(PROTO_CAP_PLAN);
    card_fsm_set_hold_only(&s_c, true);
    TEST_ASSERT_EQUAL(CARD_ALLOW_HOLD, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_COMMIT_HOLD, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, click(BTN_APPROVE).kind);
    expect_decision(commit_hold(), CARD_DECISION_APPROVE);
}

/* APPROVE down on one row, ACTION moves the cursor, APPROVE up: nothing is chosen or decided. */
static void test_cursor_move_cancels_an_approve_press(void)
{
    const btn_fsm_out_t up = {.id = BTN_APPROVE, .kind = BTN_CLICK};
    questions(1, 3, 0);
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &up, s_now).kind);
    TEST_ASSERT_EQUAL_HEX8(0, s_c.answered);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_ANSWER);

    questions(1, 2, 0);
    open_ask(PROTO_CAP_ASK);
    menu_to(CARD_ROW_TERMINAL);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &up, s_now).kind);
    TEST_ASSERT_EQUAL(CARD_VIEW_MENU, s_c.view);

    plan(3, 0, false);
    open_ask(PROTO_CAP_PLAN);
    menu_to(CARD_ROW_TERMINAL);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &up, s_now).kind);
    TEST_ASSERT_FALSE(s_c.sent);
    while (card_fsm_focused_row(&s_c) != CARD_ROW_DENY_STOP) {
        TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, click(BTN_ACTION).kind);
    }
    expect_decision(click(BTN_APPROVE), CARD_DECISION_DENY_STOP);
}

static void test_plan_complete(void)
{
    plan(3, 0, false);
    open_ask(PROTO_CAP_PLAN);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.pages);
    TEST_ASSERT_EQUAL(CARD_ALLOW_CLICK, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(CARD_SESSION_READY, card_fsm_session(&s_c));
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, click(BTN_ACTION).kind);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_APPROVE);

    open_ask(PROTO_CAP_PLAN);
    card_row_t rows[CARD_MENU_ROWS];
    TEST_ASSERT_EQUAL_UINT8(3, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_EQUAL(CARD_ROW_ACCEPT_EDITS, rows[1]);
    menu_to(CARD_ROW_ACCEPT_EDITS);
    TEST_ASSERT_EQUAL(BTN_MODE_COMMIT_HOLD, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    /* A click on the hold-only row only nudges. */
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, press(BTN_APPROVE, BTN_CANCELLED).kind);
    expect_decision(commit_hold(), CARD_DECISION_APPROVE_EDITS);

    open_ask(PROTO_CAP_PLAN);
    expect_decision(click(BTN_REJECT), CARD_DECISION_REVISE);
    open_ask(PROTO_CAP_PLAN);
    menu_to(CARD_ROW_DENY_STOP);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_DENY_STOP);
    open_ask(PROTO_CAP_PLAN);
    menu_to(CARD_ROW_TERMINAL);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_DEFER);
}

static void test_plan_pages_must_be_seen(void)
{
    plan(12, 0, false);
    open_ask(PROTO_CAP_PLAN);
    TEST_ASSERT_EQUAL_UINT8(3, s_c.pages);
    TEST_ASSERT_EQUAL(CARD_ALLOW_INACTIVE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(CARD_OUT_SHAKE, click(BTN_APPROVE).kind);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_APPROVE, s_now));
    menu_to(CARD_ROW_ACCEPT_EDITS);
    TEST_ASSERT_EQUAL(CARD_SESSION_LOCKED, card_fsm_session(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_SHAKE, click(BTN_APPROVE).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, commit_hold().kind);
    click(BTN_REJECT);
    TEST_ASSERT_EQUAL(CARD_OUT_PAGE, click(BTN_ACTION).kind);
    TEST_ASSERT_EQUAL(CARD_ALLOW_INACTIVE, card_fsm_allow(&s_c));
    click(BTN_ACTION);
    TEST_ASSERT_EQUAL(CARD_ALLOW_CLICK, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(CARD_SESSION_READY, card_fsm_session(&s_c));
    expect_decision(click(BTN_APPROVE), CARD_DECISION_APPROVE);
}

static void test_plan_cut(void)
{
    plan(5, 0, true);
    open_ask(PROTO_CAP_PLAN);
    click(BTN_ACTION);
    TEST_ASSERT_EQUAL(CARD_ALLOW_CLICK, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_APPROVE_EDITS, s_now));
    card_row_t rows[CARD_MENU_ROWS];
    TEST_ASSERT_EQUAL_UINT8(2, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_EQUAL(CARD_ROW_TERMINAL, rows[0]);
    TEST_ASSERT_EQUAL(CARD_ROW_DENY_STOP, rows[1]);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_APPROVE);
    open_ask(PROTO_CAP_PLAN);
    expect_decision(click(BTN_REJECT), CARD_DECISION_REVISE);
}

static void test_plan_more_has_no_approve(void)
{
    plan(12, 3, true);
    open_ask(PROTO_CAP_PLAN);
    click(BTN_ACTION);
    click(BTN_ACTION);
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, click(BTN_APPROVE).kind);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_APPROVE, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_APPROVE_EDITS, s_now));
    card_row_t rows[CARD_MENU_ROWS];
    TEST_ASSERT_EQUAL_UINT8(2, card_fsm_rows(&s_c, rows));
    expect_decision(click(BTN_REJECT), CARD_DECISION_REVISE);
    /* more > 0 is lossy even when the frame did not say cut. */
    plan(2, 1, false);
    open_ask(PROTO_CAP_PLAN);
    TEST_ASSERT_TRUE(s_c.cut);
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
}

static void test_plan_four_pages_has_no_approve(void)
{
    /* Sixteen steps cannot come from the wire (twelve at most); the page rule still holds on its own. */
    plan(16, 0, false);
    open_ask(PROTO_CAP_PLAN);
    TEST_ASSERT_EQUAL_UINT8(4, s_c.pages);
    for (int i = 0; i < 4; i++) {
        click(BTN_ACTION);
    }
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, click(BTN_APPROVE).kind);
    expect_decision(click(BTN_REJECT), CARD_DECISION_REVISE);
}

static void test_plan_without_steps(void)
{
    plan(0, 0, false);
    open_ask(PROTO_CAP_PLAN);
    TEST_ASSERT_EQUAL_UINT8(1, s_c.pages);
    expect_decision(click(BTN_APPROVE), CARD_DECISION_APPROVE);
}

/* Every sequence of presses on a cut plan: never approve_accept_edits. */
static void test_cut_plan_never_accepts_edits(void)
{
    static const btn_kind_t kinds[] = {BTN_CLICK, BTN_HOLD, BTN_PROGRESS, BTN_COMMIT, BTN_CANCELLED};
    uint32_t seed = 12345;
    for (int run = 0; run < 400; run++) {
        plan((uint8_t)(run % 13), (uint16_t)(run % 3 == 0 ? 0 : run % 5), true);
        open_ask(PROTO_CAP_PLAN);
        for (int step = 0; step < 60 && !s_c.sent; step++) {
            seed = seed * 1103515245u + 12345u;
            const btn_fsm_out_t out = {.id = (btn_id_t)((seed >> 16) % BTN_COUNT),
                                       .kind = kinds[(seed >> 20) % 5],
                                       .permille = (uint16_t)((seed >> 8) % 1001)};
            const card_out_t o = card_fsm_button(&s_c, &out, s_now);
            TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_APPROVE_EDITS, s_now));
            if (o.kind == CARD_OUT_DECIDE) {
                TEST_ASSERT_NOT_EQUAL(CARD_DECISION_APPROVE_EDITS, o.decision);
                card_row_t rows[CARD_MENU_ROWS];
                const uint8_t n = card_fsm_rows(&s_c, rows);
                for (uint8_t i = 0; i < n; i++) {
                    TEST_ASSERT_NOT_EQUAL(CARD_ROW_ACCEPT_EDITS, rows[i]);
                }
                /* A decision ends the run only when it would go out; the app keeps the card otherwise. */
                card_fsm_set_sent(&s_c, true);
            }
        }
    }
}

/* ---- extend and deadline ---- */

static void test_extend_limits(void)
{
    questions(1, 3, 0);
    s_e.deadline_ms = NOW + 58000;
    open_ask(PROTO_CAP_ASK);
    /* 58 s left: no extend. */
    TEST_ASSERT_FALSE(click(BTN_ACTION).extend);
    s_now = NOW + 4000;
    TEST_ASSERT_TRUE(click(BTN_ACTION).extend);
    s_now = NOW + 8999;
    TEST_ASSERT_FALSE(click(BTN_ACTION).extend);
    s_now = NOW + 9000;
    TEST_ASSERT_TRUE(press(BTN_ACTION, BTN_HOLD).extend);
    /* A deadline moves the card's own deadline; the press count against it. */
    card_fsm_set_deadline(&s_c, s_now + 70000);
    s_now += 6000;
    TEST_ASSERT_FALSE(click(BTN_ACTION).extend);
    card_fsm_set_deadline(&s_c, s_now + 1000);
    TEST_ASSERT_TRUE(click(BTN_REJECT).extend);
    /* The press that sends the answer does not extend; a swallowed press does nothing. */
    s_now += 5000;
    card_fsm_set_deadline(&s_c, s_now + 1000);
    const btn_fsm_out_t swallowed = {.id = BTN_ACTION, .kind = BTN_CLICK, .swallowed = true};
    TEST_ASSERT_FALSE(card_fsm_button(&s_c, &swallowed, s_now).extend);
    const card_out_t o = click(BTN_APPROVE);
    TEST_ASSERT_EQUAL(CARD_OUT_DECIDE, o.kind);
    TEST_ASSERT_FALSE(o.extend);
    /* Past the deadline nothing counts. */
    questions(1, 3, 0);
    open_ask(PROTO_CAP_ASK);
    card_fsm_set_deadline(&s_c, s_now);
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_APPROVE, s_now));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DEFER, s_now));
}

static void test_extend_needs_the_cap(void)
{
    questions(1, 3, 0);
    open_ask(PROTO_CAP_PLAN | PROTO_CAP_DECIDE);
    TEST_ASSERT_FALSE(click(BTN_ACTION).extend);
    plan(3, 0, false);
    open_ask(PROTO_CAP_PLAN);
    TEST_ASSERT_TRUE(click(BTN_ACTION).extend);
    plan(3, 0, false);
    open_ask(PROTO_CAP_ASK);
    TEST_ASSERT_FALSE(click(BTN_ACTION).extend);
}

/* ---- receipts ---- */

static void test_receipts(void)
{
    questions(2, 3, 0x2);
    s_slot.u.q[1].has_header = false;
    s_e.n_answers = 2;
    s_e.answers[0] = (proto_answer_t){.mask = 1u << 1};
    s_e.answers[1] = (proto_answer_t){.mask = 0x5};
    proto_dismiss_t d = {.reason = PROTO_DISMISS_ANSWERED, .outcome = PROTO_OUTCOME_ALLOW,
                         .choice = PROTO_CHOICE_ANSWERED};
    card_receipt_t r;
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Answers sent", r.title);
    TEST_ASSERT_EQUAL_STRING("Head 0: Opt 0.1 \xC2\xB7 Opt 1.0, Opt 1.2", r.detail);
    TEST_ASSERT_EQUAL(CARD_TONE_ALLOWED, r.tone);
    s_e.answers[0] = (proto_answer_t){.no_preference = true};
    s_e.answers[1] = (proto_answer_t){.mask = 0};
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Head 0: No preference \xC2\xB7 None of these", r.detail);
    d.choice = PROTO_CHOICE_DECLINE;
    d.outcome = PROTO_OUTCOME_DENY;
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Declined", r.title);
    card_receipt_dismiss(&r, &d, false, &s_e);
    TEST_ASSERT_EQUAL_STRING("Answered elsewhere", r.title);
    d.reason = PROTO_DISMISS_RELEASED;
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Moved to terminal", r.title);

    plan(3, 0, false);
    d = (proto_dismiss_t){.reason = PROTO_DISMISS_ANSWERED, .outcome = PROTO_OUTCOME_ALLOW,
                          .choice = PROTO_CHOICE_APPROVE};
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Plan approved", r.title);
    TEST_ASSERT_EQUAL_STRING("you approve edits", r.detail);
    d.choice = PROTO_CHOICE_APPROVE_ACCEPT_EDITS;
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("edits accepted", r.detail);
    d.choice = PROTO_CHOICE_REVISE;
    d.outcome = PROTO_OUTCOME_DENY;
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Sent back for changes", r.title);
    d.choice = PROTO_CHOICE_DENY_STOP;
    card_receipt_dismiss(&r, &d, true, &s_e);
    TEST_ASSERT_EQUAL_STRING("Denied", r.title);
    TEST_ASSERT_EQUAL_STRING("Claude stopped", r.detail);
}

static void test_answer_text_keeps_utf8_whole(void)
{
    questions(1, 2, 0x1);
    snprintf(s_slot.u.q[0].options[0].label, PROTO_Q_LABEL_SIZE, "Caf\xC3\xA9");
    snprintf(s_slot.u.q[0].options[1].label, PROTO_Q_LABEL_SIZE, "Na\xC3\xAFve");
    const proto_answer_t a = {.mask = 0x3};
    char out[8];
    card_answer_text(out, sizeof(out), &s_slot.u.q[0], &a);
    TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9, ", out);
    char full[32];
    card_answer_text(full, sizeof(full), &s_slot.u.q[0], &a);
    TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9, Na\xC3\xAFve", full);
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("review", card_view_name(CARD_VIEW_REVIEW));
    TEST_ASSERT_EQUAL_STRING("read", card_view_name(CARD_VIEW_READ));
    TEST_ASSERT_EQUAL_STRING("accept_edits", card_row_name(CARD_ROW_ACCEPT_EDITS));
    TEST_ASSERT_EQUAL_STRING("let_claude", card_row_name(CARD_ROW_LET_CLAUDE));
    TEST_ASSERT_EQUAL_STRING("approve_accept_edits", card_decision_name(CARD_DECISION_APPROVE_EDITS));
    TEST_ASSERT_EQUAL_STRING("no_preference", card_decision_name(CARD_DECISION_NO_PREFERENCE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_question_sends_at_once);
    RUN_TEST(test_reject_on_first_question_is_terminal);
    RUN_TEST(test_long_question_opens_on_read_page);
    RUN_TEST(test_two_lines_need_no_read_page);
    RUN_TEST(test_two_questions_review_and_back);
    RUN_TEST(test_four_questions_answers_in_order);
    RUN_TEST(test_multi_select);
    RUN_TEST(test_multi_select_none_and_back);
    RUN_TEST(test_question_menu);
    RUN_TEST(test_review_menu);
    RUN_TEST(test_question_refuses_permission_decisions);
    RUN_TEST(test_cursor_move_cancels_an_approve_press);
    RUN_TEST(test_plan_complete);
    RUN_TEST(test_plan_hold_only);
    RUN_TEST(test_plan_pages_must_be_seen);
    RUN_TEST(test_plan_cut);
    RUN_TEST(test_plan_more_has_no_approve);
    RUN_TEST(test_plan_four_pages_has_no_approve);
    RUN_TEST(test_plan_without_steps);
    RUN_TEST(test_cut_plan_never_accepts_edits);
    RUN_TEST(test_extend_limits);
    RUN_TEST(test_extend_needs_the_cap);
    RUN_TEST(test_receipts);
    RUN_TEST(test_answer_text_keeps_utf8_whole);
    RUN_TEST(test_names);
    return UNITY_END();
}
