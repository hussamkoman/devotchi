#include "card_fsm.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define DEADLINE  30000u
#define NOW       1000u

static reqq_entry_t s_e;
static card_fsm_t s_c;

/* A Bash request with card fields and a one-page command. */
static void entry(proto_risk_t risk, const char *cmd)
{
    memset(&s_e, 0, sizeof(s_e));
    snprintf(s_e.id, sizeof(s_e.id), "%032x", 1);
    snprintf(s_e.tool, sizeof(s_e.tool), "Bash");
    snprintf(s_e.cmd, sizeof(s_e.cmd), "%s", cmd);
    s_e.has_cmd = true;
    s_e.risk = risk;
    s_e.deadline_ms = DEADLINE;
}

static void grant(void)
{
    s_e.has_grant = true;
    s_e.grant_prefix = true;
    snprintf(s_e.grant_rule, sizeof(s_e.grant_rule), "pnpm test");
}

static void open_card(bool decide)
{
    card_fsm_open(&s_c, &s_e, decide);
}

/* A command of n lines that fit the well (n pages of three lines need 3n lines). */
static void lines(proto_risk_t risk, int n)
{
    static char cmd[PROTO_CMD_SIZE];
    cmd[0] = '\0';
    for (int i = 0; i < n; i++) {
        char line[16];
        snprintf(line, sizeof(line), "%sline %02d", i > 0 ? "\n" : "", i);
        strcat(cmd, line);
    }
    entry(risk, cmd);
}

static card_out_t press(btn_id_t id, btn_kind_t kind)
{
    const btn_fsm_out_t out = {.id = id, .kind = kind};
    return card_fsm_button(&s_c, &out, NOW);
}

static card_out_t progress(uint16_t permille)
{
    const btn_fsm_out_t out = {.id = BTN_APPROVE, .kind = BTN_PROGRESS, .permille = permille};
    return card_fsm_button(&s_c, &out, NOW);
}

/* A full commit hold on APPROVE as btn_fsm reports it. */
static card_out_t commit_hold(void)
{
    progress(0);
    progress(500);
    return press(BTN_APPROVE, BTN_COMMIT);
}

static void expect_decision(card_out_t o, card_decision_t d)
{
    TEST_ASSERT_EQUAL(CARD_OUT_DECIDE, o.kind);
    TEST_ASSERT_EQUAL_STRING(card_decision_name(d), card_decision_name(o.decision));
}

static void open_menu(void)
{
    const card_out_t o = press(BTN_ACTION, BTN_HOLD);
    TEST_ASSERT_EQUAL(CARD_OUT_MENU_OPEN, o.kind);
    TEST_ASSERT_EQUAL(CARD_ARM_MS, o.arm_ms);
    TEST_ASSERT_EQUAL(CARD_VIEW_MENU, s_c.view);
}

static void cursor_to(card_row_t row)
{
    for (int i = 0; i < CARD_MENU_ROWS && card_fsm_focused_row(&s_c) != row; i++) {
        TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, press(BTN_ACTION, BTN_CLICK).kind);
    }
    TEST_ASSERT_EQUAL(row, card_fsm_focused_row(&s_c));
}

/* ---- the card, low and medium risk (ux-flows 2) ---- */

static void test_low_reject_click_denies(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    open_card(true);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_REJECT, NOW));
    expect_decision(press(BTN_REJECT, BTN_CLICK), CARD_DECISION_DENY);
}

static void test_low_approve_click_allows_once(void)
{
    entry(PROTO_RISK_MED, "pnpm test");
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_ALLOW_CLICK, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    expect_decision(press(BTN_APPROVE, BTN_CLICK), CARD_DECISION_ALLOW);
}

static void test_action_click_pages_only_with_two_pages(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    open_card(true);
    TEST_ASSERT_EQUAL(BTN_MODE_NAV_HOLD, card_fsm_mode(&s_c, BTN_ACTION, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, press(BTN_ACTION, BTN_CLICK).kind);
    TEST_ASSERT_EQUAL(0, s_c.page);

    lines(PROTO_RISK_LOW, 5);
    open_card(true);
    TEST_ASSERT_EQUAL(2, s_c.pages);
    TEST_ASSERT_EQUAL(CARD_OUT_PAGE, press(BTN_ACTION, BTN_CLICK).kind);
    TEST_ASSERT_EQUAL(1, s_c.page);
    TEST_ASSERT_EQUAL(CARD_OUT_PAGE, press(BTN_ACTION, BTN_CLICK).kind);
    TEST_ASSERT_EQUAL(0, s_c.page);
    TEST_ASSERT_EQUAL(2, s_c.seen);
}

static void test_read_before_yes(void)
{
    lines(PROTO_RISK_LOW, 9);
    open_card(true);
    TEST_ASSERT_EQUAL(3, s_c.pages);
    TEST_ASSERT_EQUAL(CARD_ALLOW_INACTIVE, card_fsm_allow(&s_c));
    TEST_ASSERT_FALSE(card_fsm_allow_enabled(&s_c, true));
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_SHAKE, press(BTN_APPROVE, BTN_CLICK).kind);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW, NOW));
    press(BTN_ACTION, BTN_CLICK);
    TEST_ASSERT_EQUAL(CARD_OUT_SHAKE, press(BTN_APPROVE, BTN_CLICK).kind);
    press(BTN_ACTION, BTN_CLICK);
    TEST_ASSERT_TRUE(card_fsm_reading_done(&s_c));
    TEST_ASSERT_FALSE(card_fsm_allow_enabled(&s_c, false));
    TEST_ASSERT_TRUE(card_fsm_allow_enabled(&s_c, true));
    /* Back to page 1: everything was shown once, that is enough. */
    press(BTN_ACTION, BTN_CLICK);
    TEST_ASSERT_EQUAL(0, s_c.page);
    expect_decision(press(BTN_APPROVE, BTN_CLICK), CARD_DECISION_ALLOW);
}

static void test_more_than_four_pages_has_no_allow(void)
{
    lines(PROTO_RISK_LOW, 13);
    open_card(true);
    TEST_ASSERT_EQUAL(5, s_c.pages);
    for (int i = 0; i < 5; i++) {
        press(BTN_ACTION, BTN_CLICK);
    }
    TEST_ASSERT_EQUAL(5, s_c.seen);
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_CLICK).kind);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW, NOW));
    expect_decision(press(BTN_REJECT, BTN_CLICK), CARD_DECISION_DENY);
}

static void test_cut_has_no_allow(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    s_e.cut = true;
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW, NOW));
    TEST_ASSERT_TRUE(card_fsm_may_send(&s_c, CARD_DECISION_DENY, NOW));
    TEST_ASSERT_TRUE(card_fsm_may_send(&s_c, CARD_DECISION_DEFER, NOW));
}

static void test_edit_needs_only_the_path_page(void)
{
    static char cmd[PROTO_CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "packages/server/src/routes/hooks.ts\n+18 -4 lines");
    for (int i = 0; i < 20; i++) {
        strcat(cmd, "\n+ changed line");
    }
    entry(PROTO_RISK_MED, cmd);
    snprintf(s_e.tool, sizeof(s_e.tool), "Edit");
    open_card(true);
    TEST_ASSERT_GREATER_THAN(4, s_c.pages);
    TEST_ASSERT_EQUAL(1, s_c.required);
    TEST_ASSERT_EQUAL(CARD_ALLOW_CLICK, card_fsm_allow(&s_c));
    expect_decision(press(BTN_APPROVE, BTN_CLICK), CARD_DECISION_ALLOW);

    /* The phase-1 details of an edit are no path line: every page counts. */
    s_e.has_cmd = false;
    open_card(false);
    TEST_ASSERT_EQUAL(s_c.pages, s_c.required);
    TEST_ASSERT_EQUAL(CARD_ALLOW_NONE, card_fsm_allow(&s_c));
}

static void test_shell_command_leaves_room_for_the_prompt(void)
{
    /* 33 columns: one line in the well's 34, two behind the "$ " prompt. */
    entry(PROTO_RISK_LOW, "123456789012345678901234567890123\nb\nc");
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_COLS - 2, s_c.cols);
    TEST_ASSERT_EQUAL(2, s_c.pages);
    snprintf(s_e.tool, sizeof(s_e.tool), "Read");
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_COLS, s_c.cols);
    TEST_ASSERT_EQUAL(1, s_c.pages);
}

static void test_empty_command_needs_no_reading(void)
{
    entry(PROTO_RISK_LOW, "");
    open_card(false);
    TEST_ASSERT_EQUAL(0, s_c.pages);
    TEST_ASSERT_TRUE(card_fsm_reading_done(&s_c));
}

/* ---- the card, high risk ---- */

static void test_high_click_only_nudges(void)
{
    entry(PROTO_RISK_HIGH, "git push --force origin master");
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_ALLOW_HOLD, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_COMMIT_HOLD, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    /* In commit mode a click is a cancelled hold. */
    TEST_ASSERT_EQUAL(CARD_OUT_PROGRESS, progress(0).kind);
    const card_out_t o = press(BTN_APPROVE, BTN_CANCELLED);
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, o.kind);
    TEST_ASSERT_EQUAL(BTN_APPROVE, o.btn);
    /* A stray click output never allows a high-risk request. */
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_CLICK).kind);
    TEST_ASSERT_FALSE(s_c.sent);
}

static void test_high_hold_allows_once(void)
{
    entry(PROTO_RISK_HIGH, "git push --force origin master");
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_OUT_PROGRESS, progress(0).kind);
    const card_out_t p = progress(620);
    TEST_ASSERT_EQUAL(CARD_OUT_PROGRESS, p.kind);
    TEST_ASSERT_EQUAL(620, p.permille);
    expect_decision(press(BTN_APPROVE, BTN_COMMIT), CARD_DECISION_ALLOW);
}

static void test_high_release_at_1100_cancels(void)
{
    entry(PROTO_RISK_HIGH, "git push --force origin master");
    open_card(true);
    progress(0);
    progress(900);
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, press(BTN_APPROVE, BTN_CANCELLED).kind);
    /* A commit without a hold that began on this card does not count. */
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_COMMIT).kind);
    TEST_ASSERT_FALSE(s_c.holding);
}

static void test_high_needs_every_page_before_the_hold(void)
{
    lines(PROTO_RISK_HIGH, 4);
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_ALLOW_INACTIVE, card_fsm_allow(&s_c));
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_SHAKE, press(BTN_APPROVE, BTN_CLICK).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, commit_hold().kind);
    press(BTN_ACTION, BTN_CLICK);
    expect_decision(commit_hold(), CARD_DECISION_ALLOW);
}

static void test_commit_of_a_press_from_another_card_never_counts(void)
{
    entry(PROTO_RISK_HIGH, "git push --force origin master");
    open_card(true);
    progress(0);
    progress(800);
    /* The card is replaced under the finger. */
    s_e.id[0] = 'f';
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_COMMIT).kind);
    /* And the app's cancel does the same on the card that stays. */
    progress(0);
    card_fsm_cancel_hold(&s_c);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_COMMIT).kind);
}

static void test_timeout_during_a_hold(void)
{
    entry(PROTO_RISK_HIGH, "git push --force origin master");
    open_card(true);
    progress(0);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_tick(&s_c, DEADLINE - 1).kind);
    const card_out_t o = card_fsm_tick(&s_c, DEADLINE);
    TEST_ASSERT_EQUAL(CARD_OUT_CANCEL, o.kind);
    const btn_fsm_out_t commit = {.id = BTN_APPROVE, .kind = BTN_COMMIT};
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &commit, DEADLINE + 100).kind);
    const btn_fsm_out_t deny = {.id = BTN_REJECT, .kind = BTN_CLICK};
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &deny, DEADLINE + 100).kind);
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, BTN_REJECT, DEADLINE));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DENY, DEADLINE));
}

static void test_swallowed_outputs_do_nothing(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    open_card(true);
    const btn_fsm_out_t outs[] = {
        {.id = BTN_APPROVE, .kind = BTN_CLICK, .swallowed = true},
        {.id = BTN_REJECT, .kind = BTN_CLICK, .swallowed = true},
        {.id = BTN_ACTION, .kind = BTN_HOLD, .swallowed = true},
        {.id = BTN_APPROVE, .kind = BTN_COMMIT, .swallowed = true},
    };
    for (size_t i = 0; i < sizeof(outs) / sizeof(outs[0]); i++) {
        TEST_ASSERT_EQUAL(CARD_OUT_NONE, card_fsm_button(&s_c, &outs[i], NOW).kind);
    }
    TEST_ASSERT_EQUAL(CARD_VIEW_CARD, s_c.view);
}

static void test_sent_card_takes_no_button(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    open_card(true);
    expect_decision(press(BTN_APPROVE, BTN_CLICK), CARD_DECISION_ALLOW);
    card_fsm_set_sent(&s_c, true);
    for (int b = 0; b < BTN_COUNT; b++) {
        TEST_ASSERT_EQUAL(BTN_MODE_NONE, card_fsm_mode(&s_c, (btn_id_t)b, NOW));
    }
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_REJECT, BTN_CLICK).kind);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DENY, NOW));
    /* An error from the server brings the buttons back. */
    card_fsm_set_sent(&s_c, false);
    expect_decision(press(BTN_REJECT, BTN_CLICK), CARD_DECISION_DENY);
}

/* ---- the decision menu ---- */

static void test_menu_rows_and_focus(void)
{
    entry(PROTO_RISK_LOW, "pnpm test --filter server");
    grant();
    open_card(true);
    open_menu();
    card_row_t rows[CARD_MENU_ROWS];
    TEST_ASSERT_EQUAL(4, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_EQUAL(CARD_ROW_TERMINAL, rows[0]);
    TEST_ASSERT_EQUAL(CARD_ROW_SESSION, rows[1]);
    TEST_ASSERT_EQUAL(CARD_ROW_DENY_ASK_WHY, rows[2]);
    TEST_ASSERT_EQUAL(CARD_ROW_DENY_STOP, rows[3]);
    /* The cursor opens on the safe row, never on the granting one. */
    TEST_ASSERT_EQUAL(CARD_ROW_TERMINAL, card_fsm_focused_row(&s_c));
    for (int i = 0; i < 4; i++) {
        press(BTN_ACTION, BTN_CLICK);
    }
    TEST_ASSERT_EQUAL(CARD_ROW_TERMINAL, card_fsm_focused_row(&s_c));
    cursor_to(CARD_ROW_DENY_STOP);
    TEST_ASSERT_EQUAL(CARD_OUT_MENU_BACK, press(BTN_REJECT, BTN_CLICK).kind);
    open_menu();
    TEST_ASSERT_EQUAL(CARD_ROW_TERMINAL, card_fsm_focused_row(&s_c));
}

static void test_menu_back_arms_and_returns(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    open_card(true);
    open_menu();
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_REJECT, NOW));
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_ACTION, NOW));
    const card_out_t o = press(BTN_REJECT, BTN_CLICK);
    TEST_ASSERT_EQUAL(CARD_OUT_MENU_BACK, o.kind);
    TEST_ASSERT_EQUAL(CARD_ARM_MS, o.arm_ms);
    TEST_ASSERT_EQUAL(CARD_VIEW_CARD, s_c.view);
    TEST_ASSERT_FALSE(s_c.sent);
}

static void test_menu_rows_decide(void)
{
    static const struct {
        card_row_t row;
        card_decision_t d;
    } CASES[] = {
        {CARD_ROW_TERMINAL, CARD_DECISION_DEFER},
        {CARD_ROW_DENY_ASK_WHY, CARD_DECISION_DENY_ASK_WHY},
        {CARD_ROW_DENY_STOP, CARD_DECISION_DENY_STOP},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        entry(PROTO_RISK_MED, "pnpm test");
        grant();
        open_card(true);
        open_menu();
        cursor_to(CASES[i].row);
        TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
        expect_decision(press(BTN_APPROVE, BTN_CLICK), CASES[i].d);
    }
}

static void test_session_ready_needs_the_hold(void)
{
    entry(PROTO_RISK_LOW, "pnpm test --filter server");
    grant();
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_SESSION_READY, card_fsm_session(&s_c));
    open_menu();
    cursor_to(CARD_ROW_SESSION);
    TEST_ASSERT_EQUAL(BTN_MODE_COMMIT_HOLD, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_CLICK).kind);
    progress(0);
    TEST_ASSERT_EQUAL(CARD_OUT_NUDGE, press(BTN_APPROVE, BTN_CANCELLED).kind);
    TEST_ASSERT_TRUE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, NOW));
    expect_decision(commit_hold(), CARD_DECISION_ALLOW_SESSION);
}

static void test_session_locked_until_every_page(void)
{
    lines(PROTO_RISK_LOW, 6);
    grant();
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_SESSION_LOCKED, card_fsm_session(&s_c));
    open_menu();
    cursor_to(CARD_ROW_SESSION);
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, card_fsm_mode(&s_c, BTN_APPROVE, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_SHAKE, press(BTN_APPROVE, BTN_CLICK).kind);
    /* A hold on a locked row produces no event. */
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, progress(0).kind);
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_COMMIT).kind);
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, NOW));
    /* Paging, returning to the menu and holding is the only way to the grant. */
    press(BTN_REJECT, BTN_CLICK);
    press(BTN_ACTION, BTN_CLICK);
    TEST_ASSERT_EQUAL(CARD_SESSION_READY, card_fsm_session(&s_c));
    open_menu();
    cursor_to(CARD_ROW_SESSION);
    expect_decision(commit_hold(), CARD_DECISION_ALLOW_SESSION);
}

static void test_session_locked_over_four_pages(void)
{
    lines(PROTO_RISK_LOW, 15);
    grant();
    open_card(true);
    for (int i = 0; i < 5; i++) {
        press(BTN_ACTION, BTN_CLICK);
    }
    TEST_ASSERT_EQUAL(CARD_SESSION_LOCKED, card_fsm_session(&s_c));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, NOW));
}

static void test_session_absent(void)
{
    card_row_t rows[CARD_MENU_ROWS];
    /* No grant. */
    entry(PROTO_RISK_LOW, "pnpm test");
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
    TEST_ASSERT_EQUAL(3, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, NOW));
    /* High risk: no granting row at all. */
    entry(PROTO_RISK_HIGH, "pnpm test");
    grant();
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
    TEST_ASSERT_EQUAL(3, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, NOW));
    /* Cut and grant: absent, even if a grant slipped into the entry. */
    entry(PROTO_RISK_LOW, "pnpm test");
    grant();
    s_e.cut = true;
    open_card(true);
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
    TEST_ASSERT_EQUAL(3, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_ALLOW_SESSION, NOW));
    open_menu();
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_NOT_EQUAL(CARD_ROW_SESSION, card_fsm_focused_row(&s_c));
        TEST_ASSERT_NOT_EQUAL(CARD_OUT_DECIDE, press(BTN_ACTION, BTN_CLICK).kind);
    }
}

static void test_cursor_move_cancels_a_session_hold(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    grant();
    open_card(true);
    open_menu();
    cursor_to(CARD_ROW_SESSION);
    progress(0);
    press(BTN_ACTION, BTN_CLICK);
    TEST_ASSERT_EQUAL(CARD_ROW_DENY_ASK_WHY, card_fsm_focused_row(&s_c));
    TEST_ASSERT_EQUAL(CARD_OUT_NONE, press(BTN_APPROVE, BTN_COMMIT).kind);
}

static void test_phase1_server_menu_has_only_terminal(void)
{
    entry(PROTO_RISK_LOW, "pnpm test");
    grant();
    open_card(false);
    open_menu();
    card_row_t rows[CARD_MENU_ROWS];
    TEST_ASSERT_EQUAL(1, card_fsm_rows(&s_c, rows));
    TEST_ASSERT_EQUAL(CARD_SESSION_ABSENT, card_fsm_session(&s_c));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DENY_ASK_WHY, NOW));
    TEST_ASSERT_FALSE(card_fsm_may_send(&s_c, CARD_DECISION_DENY_STOP, NOW));
    TEST_ASSERT_EQUAL(CARD_OUT_CURSOR, press(BTN_ACTION, BTN_CLICK).kind);
    expect_decision(press(BTN_APPROVE, BTN_CLICK), CARD_DECISION_DEFER);
}

static void dismiss_receipt(card_receipt_t *r, proto_dismiss_reason_t reason, proto_outcome_t outcome,
                            proto_choice_t choice, const char *by, bool ours)
{
    proto_dismiss_t d = {.reason = reason, .outcome = outcome, .choice = choice};
    snprintf(d.by, sizeof(d.by), "%s", by);
    card_receipt_dismiss(r, &d, ours, &s_e);
}

static void test_receipts(void)
{
    card_receipt_t r;
    entry(PROTO_RISK_LOW, "pnpm test --filter server");
    grant();
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_ALLOW, PROTO_CHOICE_ALLOW_SESSION, "dv-1", true);
    TEST_ASSERT_EQUAL(CARD_TONE_ALLOWED, r.tone);
    TEST_ASSERT_EQUAL_STRING("Allowed", r.title);
    TEST_ASSERT_EQUAL_STRING("pnpm test \xE2\x80\xA6 for this session", r.detail);
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_DENY, PROTO_CHOICE_NONE, "dv-1", true);
    TEST_ASSERT_EQUAL(CARD_TONE_DENIED, r.tone);
    TEST_ASSERT_EQUAL_STRING("Claude was told to try another way", r.detail);
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_DENY, PROTO_CHOICE_DENY_ASK_WHY, "dv-1", true);
    TEST_ASSERT_EQUAL_STRING("Claude will explain why", r.detail);
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_DENY, PROTO_CHOICE_DENY_STOP, "dv-1", true);
    TEST_ASSERT_EQUAL_STRING("Claude stopped", r.detail);
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_ALLOW, PROTO_CHOICE_NONE, "dv-1", true);
    TEST_ASSERT_EQUAL_STRING("This call only", r.detail);
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_ALLOW, PROTO_CHOICE_NONE, "dashboard", false);
    TEST_ASSERT_EQUAL_STRING("Answered on dashboard", r.title);
    TEST_ASSERT_EQUAL(CARD_TONE_NEUTRAL, r.tone);
    dismiss_receipt(&r, PROTO_DISMISS_ANSWERED, PROTO_OUTCOME_ALLOW, PROTO_CHOICE_NONE, "dv-2a3b4c", false);
    TEST_ASSERT_EQUAL_STRING("Answered on dv-2a3b4c", r.title);
    dismiss_receipt(&r, PROTO_DISMISS_CANCELLED, PROTO_OUTCOME_NONE, PROTO_CHOICE_NONE, "", false);
    TEST_ASSERT_EQUAL_STRING("Answered in terminal", r.title);
    dismiss_receipt(&r, PROTO_DISMISS_TIMEOUT, PROTO_OUTCOME_NONE, PROTO_CHOICE_NONE, "", false);
    TEST_ASSERT_EQUAL_STRING("Time ran out", r.title);
    TEST_ASSERT_EQUAL_STRING("Sent to terminal", r.detail);
    dismiss_receipt(&r, PROTO_DISMISS_RELEASED, PROTO_OUTCOME_NONE, PROTO_CHOICE_NONE, "", true);
    TEST_ASSERT_EQUAL_STRING("Moved to terminal", r.title);
    TEST_ASSERT_EQUAL_STRING("", r.detail);
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("allow_session", card_decision_name(CARD_DECISION_ALLOW_SESSION));
    TEST_ASSERT_EQUAL_STRING("locked", card_session_name(CARD_SESSION_LOCKED));
    TEST_ASSERT_EQUAL_STRING("deny_stop", card_row_name(CARD_ROW_DENY_STOP));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_low_reject_click_denies);
    RUN_TEST(test_low_approve_click_allows_once);
    RUN_TEST(test_action_click_pages_only_with_two_pages);
    RUN_TEST(test_read_before_yes);
    RUN_TEST(test_more_than_four_pages_has_no_allow);
    RUN_TEST(test_cut_has_no_allow);
    RUN_TEST(test_edit_needs_only_the_path_page);
    RUN_TEST(test_shell_command_leaves_room_for_the_prompt);
    RUN_TEST(test_empty_command_needs_no_reading);
    RUN_TEST(test_high_click_only_nudges);
    RUN_TEST(test_high_hold_allows_once);
    RUN_TEST(test_high_release_at_1100_cancels);
    RUN_TEST(test_high_needs_every_page_before_the_hold);
    RUN_TEST(test_commit_of_a_press_from_another_card_never_counts);
    RUN_TEST(test_timeout_during_a_hold);
    RUN_TEST(test_swallowed_outputs_do_nothing);
    RUN_TEST(test_sent_card_takes_no_button);
    RUN_TEST(test_menu_rows_and_focus);
    RUN_TEST(test_menu_back_arms_and_returns);
    RUN_TEST(test_menu_rows_decide);
    RUN_TEST(test_session_ready_needs_the_hold);
    RUN_TEST(test_session_locked_until_every_page);
    RUN_TEST(test_session_locked_over_four_pages);
    RUN_TEST(test_session_absent);
    RUN_TEST(test_cursor_move_cancels_a_session_hold);
    RUN_TEST(test_phase1_server_menu_has_only_terminal);
    RUN_TEST(test_receipts);
    RUN_TEST(test_names);
    return UNITY_END();
}
