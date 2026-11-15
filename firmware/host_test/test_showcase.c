#include "showcase.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static showcase_act_t press(showcase_t *s, btn_id_t id, btn_kind_t kind, uint32_t now)
{
    const btn_fsm_out_t out = {.id = id, .kind = kind};
    return showcase_button(s, &out, now);
}

static showcase_act_t click(showcase_t *s, btn_id_t id, uint32_t now)
{
    return press(s, id, BTN_CLICK, now);
}

static void test_entry_refused_with_a_request(void)
{
    showcase_t s;
    showcase_init(&s);
    TEST_ASSERT_FALSE(showcase_may_enter(1));
    TEST_ASSERT_FALSE(showcase_enter(&s, 1, 0));
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_FALSE(showcase_guarded(&s, 0));
    TEST_ASSERT_TRUE(showcase_may_enter(0));
    TEST_ASSERT_TRUE(showcase_enter(&s, 0, 0));
    TEST_ASSERT_TRUE(s.active);
    TEST_ASSERT_EQUAL(SHOWCASE_PAGE_MENU, s.page);
    TEST_ASSERT_EQUAL(SHOWCASE_ROW_MOODS, s.cursor);
}

static void test_menu(void)
{
    showcase_t s;
    showcase_init(&s);
    showcase_enter(&s, 0, 0);
    for (int i = 1; i <= SHOWCASE_ROW_COUNT; i++) {
        TEST_ASSERT_EQUAL(SHOWCASE_ACT_MOVED, click(&s, BTN_ACTION, 0).kind);
        TEST_ASSERT_EQUAL(i % SHOWCASE_ROW_COUNT, s.cursor);
    }
    showcase_act_t a = click(&s, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_OPEN, a.kind);
    TEST_ASSERT_EQUAL(SHOWCASE_PAGE_MOODS, a.page);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_BACK, click(&s, BTN_REJECT, 0).kind);
    s.cursor = SHOWCASE_ROW_NOTIFY;
    a = click(&s, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_NOTIFY, a.kind);
    TEST_ASSERT_EQUAL(SHOWCASE_PAGE_NOTIFY, s.page);
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, showcase_btn_mode(&s, BTN_ACTION));
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_PLAY, click(&s, BTN_APPROVE, 0).kind);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_BACK, click(&s, BTN_REJECT, 0).kind);
    TEST_ASSERT_EQUAL(SHOWCASE_PAGE_MENU, s.page);
    s.cursor = SHOWCASE_ROW_PERMISSION;
    TEST_ASSERT_EQUAL(SHOWCASE_PAGE_PERMISSION, click(&s, BTN_APPROVE, 0).page);
    s.page = SHOWCASE_PAGE_MENU;
    s.cursor = SHOWCASE_ROW_QUESTION;
    TEST_ASSERT_EQUAL(SHOWCASE_PAGE_QUESTION, click(&s, BTN_APPROVE, 0).page);
    s.page = SHOWCASE_PAGE_MENU;
    s.cursor = SHOWCASE_ROW_LEAVE;
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_LEAVE, click(&s, BTN_APPROVE, 100).kind);
    TEST_ASSERT_FALSE(s.active);
}

static void test_pages(void)
{
    showcase_t s;
    showcase_init(&s);
    showcase_enter(&s, 0, 0);
    showcase_open(&s, SHOWCASE_PAGE_MOODS, 0);
    for (int i = 1; i <= SHOWCASE_MOODS; i++) {
        click(&s, BTN_ACTION, 0);
        TEST_ASSERT_EQUAL(i % SHOWCASE_MOODS, s.mood);
    }
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_PLAY, click(&s, BTN_APPROVE, 0).kind);
    showcase_open(&s, SHOWCASE_PAGE_REACTIONS, 0);
    for (int i = 1; i <= SHOWCASE_REACTIONS; i++) {
        click(&s, BTN_ACTION, 0);
        TEST_ASSERT_EQUAL(i % SHOWCASE_REACTIONS, s.reaction);
    }
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_PLAY, click(&s, BTN_APPROVE, 0).kind);
    showcase_open(&s, SHOWCASE_PAGE_CLOCK, 0);
    TEST_ASSERT_FALSE(s.night);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_NIGHT, click(&s, BTN_ACTION, 0).kind);
    TEST_ASSERT_TRUE(s.night);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_NONE, click(&s, BTN_APPROVE, 0).kind);
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, showcase_btn_mode(&s, BTN_APPROVE));
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_BACK, click(&s, BTN_REJECT, 0).kind);
    TEST_ASSERT_EQUAL(BTN_MODE_NAV_HOLD, showcase_btn_mode(&s, BTN_REJECT));
    TEST_ASSERT_EQUAL(BTN_MODE_CLICK, showcase_btn_mode(&s, BTN_APPROVE));
}

static void test_card_pages_pass_presses_on(void)
{
    showcase_t s;
    showcase_init(&s);
    showcase_enter(&s, 0, 0);
    showcase_open(&s, SHOWCASE_PAGE_PERMISSION, 0);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_CARD, click(&s, BTN_REJECT, 0).kind);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_CARD, press(&s, BTN_APPROVE, BTN_PROGRESS, 0).kind);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_CARD, press(&s, BTN_APPROVE, BTN_COMMIT, 0).kind);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_CARD, press(&s, BTN_ACTION, BTN_HOLD, 0).kind);
    TEST_ASSERT_TRUE(s.active);
    /* REJECT hold leaves from every page. */
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_LEAVE, press(&s, BTN_REJECT, BTN_HOLD, 0).kind);
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_NONE, click(&s, BTN_ACTION, 0).kind);
}

static void test_swallowed_presses_do_nothing(void)
{
    showcase_t s;
    showcase_init(&s);
    showcase_enter(&s, 0, 0);
    const btn_fsm_out_t out = {.id = BTN_REJECT, .kind = BTN_CLICK, .swallowed = true};
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_NONE, showcase_button(&s, &out, 0).kind);
    TEST_ASSERT_TRUE(s.active);
}

static void test_idle_leave_and_guard(void)
{
    showcase_t s;
    showcase_init(&s);
    showcase_enter(&s, 0, 1000);
    click(&s, BTN_ACTION, 2000);
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_NONE, showcase_tick(&s, 2000 + SHOWCASE_IDLE_MS - 1).kind);
    TEST_ASSERT_TRUE(showcase_guarded(&s, 2000 + SHOWCASE_IDLE_MS - 1));
    const uint32_t left = 2000 + SHOWCASE_IDLE_MS;
    TEST_ASSERT_EQUAL(SHOWCASE_ACT_LEAVE, showcase_tick(&s, left).kind);
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_TRUE(showcase_guarded(&s, left + SHOWCASE_GUARD_MS - 1));
    TEST_ASSERT_FALSE(showcase_guarded(&s, left + SHOWCASE_GUARD_MS));
    TEST_ASSERT_FALSE(s.guard);
    /* Leaving twice does not restart the guard. */
    showcase_leave(&s, left + 50000);
    TEST_ASSERT_FALSE(showcase_guarded(&s, left + 50001));
}

static void test_reentry_after_guard(void)
{
    showcase_t s;
    showcase_init(&s);
    showcase_enter(&s, 0, 0);
    showcase_leave(&s, 100);
    TEST_ASSERT_TRUE(showcase_enter(&s, 0, 200));
    TEST_ASSERT_FALSE(s.guard);
    TEST_ASSERT_TRUE(showcase_guarded(&s, 200));
}

static void test_names(void)
{
    showcase_page_t p;
    for (int i = 0; i < SHOWCASE_PAGE_COUNT; i++) {
        TEST_ASSERT_TRUE(showcase_page_parse(showcase_page_name((showcase_page_t)i), &p));
        TEST_ASSERT_EQUAL(i, p);
    }
    TEST_ASSERT_FALSE(showcase_page_parse("nope", &p));
    TEST_ASSERT_EQUAL_STRING("", showcase_page_name(SHOWCASE_PAGE_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_entry_refused_with_a_request);
    RUN_TEST(test_menu);
    RUN_TEST(test_pages);
    RUN_TEST(test_card_pages_pass_presses_on);
    RUN_TEST(test_swallowed_presses_do_nothing);
    RUN_TEST(test_idle_leave_and_guard);
    RUN_TEST(test_reentry_after_guard);
    RUN_TEST(test_names);
    return UNITY_END();
}
