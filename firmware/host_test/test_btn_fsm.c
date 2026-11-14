#include "btn_fsm.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static btn_fsm_t s_fsm;
static btn_fsm_out_t s_out;

static bool feed(btn_id_t id, btn_raw_t raw, uint32_t now)
{
    s_out = (btn_fsm_out_t){.id = BTN_COUNT};
    return btn_fsm_raw(&s_fsm, id, raw, now, &s_out);
}

static void expect_out(btn_id_t id, btn_kind_t kind, bool swallowed)
{
    TEST_ASSERT_EQUAL(id, s_out.id);
    TEST_ASSERT_EQUAL(kind, s_out.kind);
    TEST_ASSERT_EQUAL(swallowed, s_out.swallowed);
}

static void test_click_on_release(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_FALSE(feed(BTN_APPROVE, BTN_RAW_DOWN, 100));
    TEST_ASSERT_TRUE(feed(BTN_APPROVE, BTN_RAW_UP, 180));
    expect_out(BTN_APPROVE, BTN_CLICK, false);
}

static void test_long_suppresses_click(void)
{
    btn_fsm_init(&s_fsm);
    feed(BTN_ACTION, BTN_RAW_DOWN, 0);
    TEST_ASSERT_TRUE(feed(BTN_ACTION, BTN_RAW_LONG_START, 1000));
    expect_out(BTN_ACTION, BTN_LONG, false);
    TEST_ASSERT_FALSE(feed(BTN_ACTION, BTN_RAW_LONG_START, 1001));
    TEST_ASSERT_FALSE(feed(BTN_ACTION, BTN_RAW_UP, 1500));

    TEST_ASSERT_FALSE(feed(BTN_ACTION, BTN_RAW_DOWN, 2000));
    TEST_ASSERT_TRUE(feed(BTN_ACTION, BTN_RAW_UP, 2100));
    expect_out(BTN_ACTION, BTN_CLICK, false);
}

static void test_stray_events_ignored(void)
{
    btn_fsm_init(&s_fsm);
    TEST_ASSERT_FALSE(feed(BTN_REJECT, BTN_RAW_UP, 10));
    TEST_ASSERT_FALSE(feed(BTN_REJECT, BTN_RAW_LONG_START, 20));
    TEST_ASSERT_FALSE(btn_fsm_raw(&s_fsm, BTN_COUNT, BTN_RAW_DOWN, 30, &s_out));
}

static void test_two_buttons_overlapping(void)
{
    btn_fsm_init(&s_fsm);
    feed(BTN_APPROVE, BTN_RAW_DOWN, 0);
    feed(BTN_REJECT, BTN_RAW_DOWN, 50);
    TEST_ASSERT_TRUE(feed(BTN_APPROVE, BTN_RAW_UP, 100));
    expect_out(BTN_APPROVE, BTN_CLICK, false);
    TEST_ASSERT_TRUE(feed(BTN_REJECT, BTN_RAW_LONG_START, 1050));
    expect_out(BTN_REJECT, BTN_LONG, false);
    TEST_ASSERT_FALSE(feed(BTN_REJECT, BTN_RAW_UP, 1200));
}

static void test_swallow_window(void)
{
    btn_fsm_init(&s_fsm);
    /* The waking press: the app arms on its DOWN. */
    btn_fsm_arm_swallow(&s_fsm, 1000);
    feed(BTN_APPROVE, BTN_RAW_DOWN, 1000);
    TEST_ASSERT_TRUE(feed(BTN_APPROVE, BTN_RAW_UP, 1080));
    expect_out(BTN_APPROVE, BTN_CLICK, true);

    /* A second press inside the guard is swallowed too. */
    feed(BTN_REJECT, BTN_RAW_DOWN, 1200);
    TEST_ASSERT_TRUE(feed(BTN_REJECT, BTN_RAW_UP, 1250));
    expect_out(BTN_REJECT, BTN_CLICK, true);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 1299));

    /* After the guard with everything released: delivered. */
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 1300));
    feed(BTN_REJECT, BTN_RAW_DOWN, 1400);
    TEST_ASSERT_TRUE(feed(BTN_REJECT, BTN_RAW_UP, 1450));
    expect_out(BTN_REJECT, BTN_CLICK, false);
}

static void test_arm_after_down(void)
{
    btn_fsm_init(&s_fsm);
    feed(BTN_ACTION, BTN_RAW_DOWN, 500);
    btn_fsm_arm_swallow(&s_fsm, 500);
    TEST_ASSERT_TRUE(feed(BTN_ACTION, BTN_RAW_UP, 600));
    expect_out(BTN_ACTION, BTN_CLICK, true);
}

static void test_press_spanning_guard(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm_swallow(&s_fsm, 0);
    feed(BTN_ACTION, BTN_RAW_DOWN, 0);
    /* Held past the guard and past the long-press threshold: still the waking press. */
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 500));
    TEST_ASSERT_TRUE(feed(BTN_ACTION, BTN_RAW_LONG_START, 1000));
    expect_out(BTN_ACTION, BTN_LONG, true);
    TEST_ASSERT_FALSE(feed(BTN_ACTION, BTN_RAW_UP, 1400));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 1400));

    feed(BTN_ACTION, BTN_RAW_DOWN, 1500);
    TEST_ASSERT_TRUE(feed(BTN_ACTION, BTN_RAW_UP, 1550));
    expect_out(BTN_ACTION, BTN_CLICK, false);
}

static void test_short_press_spanning_guard(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm_swallow(&s_fsm, 0);
    feed(BTN_APPROVE, BTN_RAW_DOWN, 0);
    TEST_ASSERT_TRUE(feed(BTN_APPROVE, BTN_RAW_UP, 450));
    expect_out(BTN_APPROVE, BTN_CLICK, true);
}

static void test_guard_held_by_other_button(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm_swallow(&s_fsm, 0);
    feed(BTN_APPROVE, BTN_RAW_DOWN, 0);
    /* REJECT goes down after the guard time while APPROVE is still held. */
    feed(BTN_REJECT, BTN_RAW_DOWN, 400);
    TEST_ASSERT_TRUE(feed(BTN_APPROVE, BTN_RAW_UP, 500));
    expect_out(BTN_APPROVE, BTN_CLICK, true);
    TEST_ASSERT_TRUE(feed(BTN_REJECT, BTN_RAW_UP, 600));
    expect_out(BTN_REJECT, BTN_CLICK, true);
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 600));
}

static void test_inject(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_inject(&s_fsm, BTN_REJECT, BTN_LONG, 10, &s_out);
    expect_out(BTN_REJECT, BTN_LONG, false);

    btn_fsm_arm_swallow(&s_fsm, 100);
    btn_fsm_inject(&s_fsm, BTN_APPROVE, BTN_CLICK, 200, &s_out);
    expect_out(BTN_APPROVE, BTN_CLICK, true);
    btn_fsm_inject(&s_fsm, BTN_APPROVE, BTN_CLICK, 400, &s_out);
    expect_out(BTN_APPROVE, BTN_CLICK, false);

    /* A physical button held keeps injected events swallowed. */
    btn_fsm_arm_swallow(&s_fsm, 1000);
    feed(BTN_ACTION, BTN_RAW_DOWN, 1000);
    btn_fsm_inject(&s_fsm, BTN_APPROVE, BTN_CLICK, 2000, &s_out);
    TEST_ASSERT_TRUE(s_out.swallowed);
}

static void test_wraparound(void)
{
    btn_fsm_init(&s_fsm);
    btn_fsm_arm_swallow(&s_fsm, UINT32_MAX - 100);
    TEST_ASSERT_TRUE(btn_fsm_swallowing(&s_fsm, 50));
    TEST_ASSERT_FALSE(btn_fsm_swallowing(&s_fsm, 200));
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
    TEST_ASSERT_EQUAL_STRING("long", btn_kind_name(BTN_LONG));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_click_on_release);
    RUN_TEST(test_long_suppresses_click);
    RUN_TEST(test_stray_events_ignored);
    RUN_TEST(test_two_buttons_overlapping);
    RUN_TEST(test_swallow_window);
    RUN_TEST(test_arm_after_down);
    RUN_TEST(test_press_spanning_guard);
    RUN_TEST(test_short_press_spanning_guard);
    RUN_TEST(test_guard_held_by_other_button);
    RUN_TEST(test_inject);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_names);
    return UNITY_END();
}
