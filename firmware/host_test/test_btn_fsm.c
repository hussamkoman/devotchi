#include "btn_fsm.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static btn_fsm_t s_fsm;
static btn_fsm_out_t s_out;
static btn_fsm_out_t s_ticks[BTN_COUNT];

static bool down(btn_id_t id, btn_mode_t mode, uint32_t now)
{
    s_out = (btn_fsm_out_t){.id = BTN_COUNT};
    return btn_fsm_down(&s_fsm, id, mode, now, &s_out);
}

static bool up(btn_id_t id, uint32_t now)
{
    s_out = (btn_fsm_out_t){.id = BTN_COUNT};
    return btn_fsm_up(&s_fsm, id, now, &s_out);
}

static size_t tick(uint32_t now)
{
    return btn_fsm_tick(&s_fsm, now, s_ticks);
}

static void expect(const btn_fsm_out_t *o, btn_id_t id, btn_kind_t kind, bool swallowed)
{
    TEST_ASSERT_EQUAL(id, o->id);
    TEST_ASSERT_EQUAL(kind, o->kind);
    TEST_ASSERT_EQUAL(swallowed, o->swallowed);
}

static void test_click_mode(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_FALSE(down(BTN_APPROVE, BTN_MODE_CLICK, 100));
    TEST_ASSERT_EQUAL(0, tick(2000));
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 2100));
    expect(&s_out, BTN_APPROVE, BTN_CLICK, false);
}

static void test_none_mode_is_silent(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_FALSE(down(BTN_APPROVE, BTN_MODE_NONE, 0));
    TEST_ASSERT_EQUAL(0, tick(1500));
    TEST_ASSERT_FALSE(up(BTN_APPROVE, 1600));
}

static void test_nav_hold_click_before_threshold(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_ACTION, BTN_MODE_NAV_HOLD, 0);
    TEST_ASSERT_EQUAL(0, tick(590));
    TEST_ASSERT_TRUE(up(BTN_ACTION, 599));
    expect(&s_out, BTN_ACTION, BTN_CLICK, false);
}

static void test_nav_hold_fires_at_threshold(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_ACTION, BTN_MODE_NAV_HOLD, 0);
    TEST_ASSERT_EQUAL(0, tick(570));
    TEST_ASSERT_EQUAL(1, tick(600));
    expect(&s_ticks[0], BTN_ACTION, BTN_HOLD, false);
    TEST_ASSERT_EQUAL(0, tick(630));
    TEST_ASSERT_FALSE(up(BTN_ACTION, 2000));
}

static void test_nav_hold_due_on_release_between_ticks(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_REJECT, BTN_MODE_NAV_HOLD, 0);
    TEST_ASSERT_EQUAL(0, tick(590));
    TEST_ASSERT_TRUE(up(BTN_REJECT, 610));
    expect(&s_out, BTN_REJECT, BTN_HOLD, false);
}

static void test_commit_hold_progress_and_commit(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_TRUE(down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, 1000));
    expect(&s_out, BTN_APPROVE, BTN_PROGRESS, false);
    TEST_ASSERT_EQUAL(0, s_out.permille);
    TEST_ASSERT_EQUAL(1, tick(1600));
    expect(&s_ticks[0], BTN_APPROVE, BTN_PROGRESS, false);
    TEST_ASSERT_EQUAL(500, s_ticks[0].permille);
    TEST_ASSERT_EQUAL(1, tick(2199));
    TEST_ASSERT_EQUAL(BTN_PROGRESS, s_ticks[0].kind);
    TEST_ASSERT_EQUAL(999, s_ticks[0].permille);
    TEST_ASSERT_EQUAL(1, tick(2200));
    expect(&s_ticks[0], BTN_APPROVE, BTN_COMMIT, false);
    TEST_ASSERT_EQUAL(0, tick(2230));
    TEST_ASSERT_FALSE(up(BTN_APPROVE, 2500));
}

static void test_commit_hold_release_at_1100_cancels(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, 0);
    tick(1080);
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 1100));
    expect(&s_out, BTN_APPROVE, BTN_CANCELLED, false);
    TEST_ASSERT_EQUAL(916, s_out.permille);
}

static void test_commit_hold_click_is_cancel(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, 0);
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 80));
    expect(&s_out, BTN_APPROVE, BTN_CANCELLED, false);
}

static void test_commit_due_on_release_between_ticks(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, 0);
    tick(1190);
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 1210));
    expect(&s_out, BTN_APPROVE, BTN_COMMIT, false);
}

static void test_mode_latched_at_down(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_APPROVE, BTN_MODE_CLICK, 0);
    /* A later press of another button in another mode does not change the first. */
    down(BTN_ACTION, BTN_MODE_COMMIT_HOLD, 10);
    TEST_ASSERT_EQUAL(1, tick(1300));
    expect(&s_ticks[0], BTN_ACTION, BTN_COMMIT, false);
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 1400));
    expect(&s_out, BTN_APPROVE, BTN_CLICK, false);
}

static void test_stray_up_ignored(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_FALSE(up(BTN_REJECT, 10));
    TEST_ASSERT_FALSE(down(BTN_COUNT, BTN_MODE_CLICK, 10));
    TEST_ASSERT_FALSE(up(BTN_COUNT, 10));
    TEST_ASSERT_EQUAL(0, tick(100));
}

static void test_two_buttons_overlapping(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_APPROVE, BTN_MODE_NAV_HOLD, 0);
    down(BTN_REJECT, BTN_MODE_NAV_HOLD, 100);
    TEST_ASSERT_EQUAL(1, tick(600));
    expect(&s_ticks[0], BTN_APPROVE, BTN_HOLD, false);
    TEST_ASSERT_TRUE(up(BTN_REJECT, 650));
    expect(&s_out, BTN_REJECT, BTN_CLICK, false);
    TEST_ASSERT_FALSE(up(BTN_APPROVE, 700));
}

static void test_arm_swallows_until_delay_after_release(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm(&s_fsm, 1000, 500);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 1499));
    uint32_t until = 0;
    TEST_ASSERT_TRUE(btn_fsm_armed_until(&s_fsm, &until));
    TEST_ASSERT_EQUAL_UINT32(1500, until);
    down(BTN_APPROVE, BTN_MODE_CLICK, 1200);
    TEST_ASSERT_FALSE(btn_fsm_armed_until(&s_fsm, &until));
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 1300));
    expect(&s_out, BTN_APPROVE, BTN_CLICK, true);
    /* The delay restarts when the last button is released. */
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 1799));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 1800));
    down(BTN_APPROVE, BTN_MODE_CLICK, 1900);
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 1950));
    expect(&s_out, BTN_APPROVE, BTN_CLICK, false);
    TEST_ASSERT_FALSE(btn_fsm_armed_until(&s_fsm, &until));
}

static void test_press_before_arm_swallowed_to_its_end(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_TRUE(down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, 0));
    btn_fsm_arm(&s_fsm, 500, 500);
    /* No progress for a swallowed press, and its commit is flagged. */
    TEST_ASSERT_EQUAL(0, tick(900));
    TEST_ASSERT_EQUAL(1, tick(1200));
    expect(&s_ticks[0], BTN_APPROVE, BTN_COMMIT, true);
    TEST_ASSERT_FALSE(up(BTN_APPROVE, 5000));
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 5499));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 5500));
}

static void test_press_before_arm_short_and_hold(void)
{
    btn_fsm_init(&s_fsm);
    down(BTN_REJECT, BTN_MODE_CLICK, 0);
    btn_fsm_arm(&s_fsm, 10, 300);
    TEST_ASSERT_TRUE(up(BTN_REJECT, 20));
    expect(&s_out, BTN_REJECT, BTN_CLICK, true);
    down(BTN_ACTION, BTN_MODE_NAV_HOLD, 1000);
    btn_fsm_arm(&s_fsm, 1100, 300);
    TEST_ASSERT_EQUAL(1, tick(1600));
    expect(&s_ticks[0], BTN_ACTION, BTN_HOLD, true);
}

/* A zero delay (the card closed under the finger) swallows only the presses already down. */
static void test_zero_arm_swallows_only_held_presses(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm(&s_fsm, 0, 0);
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 0));
    down(BTN_ACTION, BTN_MODE_NAV_HOLD, 100);
    btn_fsm_arm(&s_fsm, 300, 0);
    TEST_ASSERT_EQUAL(1, tick(700));
    expect(&s_ticks[0], BTN_ACTION, BTN_HOLD, true);
    TEST_ASSERT_FALSE(up(BTN_ACTION, 800));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 800));
    TEST_ASSERT_TRUE(down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, 900));
    TEST_ASSERT_FALSE(s_out.swallowed);
}

static void test_arming_held_by_other_button(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm(&s_fsm, 0, 300);
    down(BTN_REJECT, BTN_MODE_CLICK, 100);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 1000));
    down(BTN_APPROVE, BTN_MODE_CLICK, 1000);
    TEST_ASSERT_TRUE(up(BTN_APPROVE, 1100));
    expect(&s_out, BTN_APPROVE, BTN_CLICK, true);
    TEST_ASSERT_TRUE(up(BTN_REJECT, 1200));
    TEST_ASSERT_TRUE(s_out.swallowed);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 1499));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 1500));
}

static void test_longer_arming_kept(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm(&s_fsm, 0, 1000);
    btn_fsm_arm(&s_fsm, 100, 500);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 999));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 1000));
    btn_fsm_arm(&s_fsm, 2000, 300);
    btn_fsm_arm(&s_fsm, 2100, 500);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 2599));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 2600));
}

static void test_wraparound(void)
{
    btn_fsm_init(&s_fsm);
    const uint32_t t0 = UINT32_MAX - 100;
    btn_fsm_arm(&s_fsm, t0, BTN_FSM_WAKE_GUARD_MS);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, t0 + 299));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, t0 + 300));
    down(BTN_APPROVE, BTN_MODE_COMMIT_HOLD, t0 + 400);
    TEST_ASSERT_EQUAL(1, tick(t0 + 400 + BTN_FSM_COMMIT_HOLD_MS));
    expect(&s_ticks[0], BTN_APPROVE, BTN_COMMIT, false);
}

static void test_names(void)
{
    btn_id_t id;
    TEST_ASSERT_TRUE(btn_id_parse("approve", &id));
    TEST_ASSERT_EQUAL(BTN_APPROVE, id);
    TEST_ASSERT_TRUE(btn_id_parse("action", &id));
    TEST_ASSERT_EQUAL(BTN_ACTION, id);
    TEST_ASSERT_FALSE(btn_id_parse("Approve", &id));
    TEST_ASSERT_FALSE(btn_id_parse("", &id));
    TEST_ASSERT_EQUAL_STRING("reject", btn_id_name(BTN_REJECT));
    TEST_ASSERT_EQUAL_STRING("", btn_id_name(BTN_COUNT));
    TEST_ASSERT_EQUAL_STRING("hold", btn_kind_name(BTN_HOLD));
    TEST_ASSERT_EQUAL_STRING("cancelled", btn_kind_name(BTN_CANCELLED));
    TEST_ASSERT_EQUAL_STRING("commit_hold", btn_mode_name(BTN_MODE_COMMIT_HOLD));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_click_mode);
    RUN_TEST(test_none_mode_is_silent);
    RUN_TEST(test_nav_hold_click_before_threshold);
    RUN_TEST(test_nav_hold_fires_at_threshold);
    RUN_TEST(test_nav_hold_due_on_release_between_ticks);
    RUN_TEST(test_commit_hold_progress_and_commit);
    RUN_TEST(test_commit_hold_release_at_1100_cancels);
    RUN_TEST(test_commit_hold_click_is_cancel);
    RUN_TEST(test_commit_due_on_release_between_ticks);
    RUN_TEST(test_mode_latched_at_down);
    RUN_TEST(test_stray_up_ignored);
    RUN_TEST(test_two_buttons_overlapping);
    RUN_TEST(test_arm_swallows_until_delay_after_release);
    RUN_TEST(test_press_before_arm_swallowed_to_its_end);
    RUN_TEST(test_press_before_arm_short_and_hold);
    RUN_TEST(test_zero_arm_swallows_only_held_presses);
    RUN_TEST(test_arming_held_by_other_button);
    RUN_TEST(test_longer_arming_kept);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_names);
    return UNITY_END();
}
