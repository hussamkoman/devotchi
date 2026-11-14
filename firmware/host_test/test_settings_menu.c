#include "settings_menu.h"

#include "btn_fsm.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint16_t DEFAULTS[SETTINGS_VALUE_ROWS] = {
    [SETTINGS_ROW_BRIGHTNESS] = 75, [SETTINGS_ROW_SCREEN_TIMEOUT] = 30, [SETTINGS_ROW_DEEP_SLEEP] = 20,
    [SETTINGS_ROW_HAPTICS] = 2,     [SETTINGS_ROW_LED] = 1,             [SETTINGS_ROW_MODE] = SETTINGS_MODE_ASK,
};

static settings_act_t click(settings_menu_t *m, btn_id_t id, uint32_t now)
{
    const btn_fsm_out_t out = {.id = id, .kind = BTN_CLICK};
    return settings_menu_button(m, &out, now);
}

static settings_act_t hold(settings_menu_t *m, btn_id_t id, uint32_t now)
{
    const btn_fsm_out_t out = {.id = id, .kind = BTN_LONG};
    return settings_menu_button(m, &out, now);
}

static void go_to(settings_menu_t *m, settings_row_t row)
{
    while (m->cursor != row) {
        click(m, BTN_APPROVE, 0);
    }
}

static void test_open_and_navigation(void)
{
    settings_menu_t m;
    settings_menu_open(&m, DEFAULTS);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_BRIGHTNESS, m.cursor);
    TEST_ASSERT_FALSE(m.confirming);
    TEST_ASSERT_EQUAL_UINT16(75, m.values[SETTINGS_ROW_BRIGHTNESS]);

    settings_act_t a = click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_MOVED, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_SCREEN_TIMEOUT, a.row);
    a = click(&m, BTN_REJECT, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_BRIGHTNESS, a.row);
    /* Up from the first row wraps to the last, down from the last to the first. */
    a = click(&m, BTN_REJECT, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_ABOUT, a.row);
    a = click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_BRIGHTNESS, a.row);
    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        click(&m, BTN_APPROVE, 0);
    }
    TEST_ASSERT_EQUAL(SETTINGS_ROW_BRIGHTNESS, m.cursor);

    /* Long presses of APPROVE/REJECT and swallowed outputs do nothing. */
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, hold(&m, BTN_APPROVE, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, hold(&m, BTN_REJECT, 0).kind);
    const btn_fsm_out_t swallowed = {.id = BTN_ACTION, .kind = BTN_CLICK, .swallowed = true};
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_button(&m, &swallowed, 0).kind);
    const btn_fsm_out_t swallowed_long = {.id = BTN_ACTION, .kind = BTN_LONG, .swallowed = true};
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_button(&m, &swallowed_long, 0).kind);
    TEST_ASSERT_EQUAL_UINT16(75, m.values[SETTINGS_ROW_BRIGHTNESS]);

    a = hold(&m, BTN_ACTION, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, a.kind);
}

static void test_value_cycling(void)
{
    static const struct {
        settings_row_t row;
        uint16_t sequence[5];
        size_t count;
    } CASES[] = {
        {SETTINGS_ROW_BRIGHTNESS, {100, 25, 50, 75, 100}, 5},
        {SETTINGS_ROW_SCREEN_TIMEOUT, {60, 120, 15, 30, 60}, 5},
        {SETTINGS_ROW_DEEP_SLEEP, {60, 0, 10, 20, 60}, 5},
        {SETTINGS_ROW_HAPTICS, {0, 1, 2, 0}, 4},
        {SETTINGS_ROW_LED, {0, 1, 0}, 3},
        {SETTINGS_ROW_MODE, {SETTINGS_MODE_DESK, SETTINGS_MODE_ASK, SETTINGS_MODE_DESK}, 3},
    };
    for (size_t c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++) {
        settings_menu_t m;
        settings_menu_open(&m, DEFAULTS);
        go_to(&m, CASES[c].row);
        for (size_t i = 0; i < CASES[c].count; i++) {
            const settings_act_t a = click(&m, BTN_ACTION, 0);
            TEST_ASSERT_EQUAL(SETTINGS_ACT_VALUE, a.kind);
            TEST_ASSERT_EQUAL(CASES[c].row, a.row);
            TEST_ASSERT_EQUAL_UINT16(CASES[c].sequence[i], a.value);
            TEST_ASSERT_EQUAL_UINT16(a.value, m.values[a.row]);
            TEST_ASSERT_TRUE(settings_menu_allowed(a.row, a.value));
        }
        TEST_ASSERT_FALSE(m.confirming);
    }

    /* A stored value outside the table restarts at the first choice. */
    uint16_t odd[SETTINGS_VALUE_ROWS];
    memcpy(odd, DEFAULTS, sizeof(odd));
    odd[SETTINGS_ROW_BRIGHTNESS] = 33;
    settings_menu_t m;
    settings_menu_open(&m, odd);
    TEST_ASSERT_EQUAL_UINT16(25, click(&m, BTN_ACTION, 0).value);

    /* The room mode is synced from outside; other rows are not touched by a sync past the value rows. */
    settings_menu_sync(&m, SETTINGS_ROW_MODE, SETTINGS_MODE_DESK);
    TEST_ASSERT_EQUAL_UINT16(SETTINGS_MODE_DESK, m.values[SETTINGS_ROW_MODE]);
    settings_menu_sync(&m, SETTINGS_ROW_ABOUT, 7);
    TEST_ASSERT_EQUAL_UINT16(25, m.values[SETTINGS_ROW_BRIGHTNESS]);
}

static void test_about_executes_at_once(void)
{
    settings_menu_t m;
    settings_menu_open(&m, DEFAULTS);
    go_to(&m, SETTINGS_ROW_ABOUT);
    const settings_act_t a = click(&m, BTN_ACTION, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXECUTE, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_ABOUT, a.row);
    TEST_ASSERT_FALSE(settings_menu_needs_confirm(SETTINGS_ROW_ABOUT));
}

static void test_confirm_twice(void)
{
    static const settings_row_t ROWS[] = {SETTINGS_ROW_WIFI_SETUP, SETTINGS_ROW_UNPAIR, SETTINGS_ROW_FACTORY_RESET};
    for (size_t r = 0; r < 3; r++) {
        settings_menu_t m;
        settings_menu_open(&m, DEFAULTS);
        go_to(&m, ROWS[r]);
        TEST_ASSERT_TRUE(settings_menu_needs_confirm(ROWS[r]));
        settings_act_t a = click(&m, BTN_ACTION, 1000);
        TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_ASK, a.kind);
        TEST_ASSERT_TRUE(m.confirming);
        TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_tick(&m, 1000 + SETTINGS_CONFIRM_MS - 1).kind);
        a = click(&m, BTN_ACTION, 1000 + SETTINGS_CONFIRM_MS - 1);
        TEST_ASSERT_EQUAL(SETTINGS_ACT_EXECUTE, a.kind);
        TEST_ASSERT_EQUAL(ROWS[r], a.row);
        TEST_ASSERT_FALSE(m.confirming);
        /* Executing ends the confirmation: the next click asks again. */
        TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_ASK, click(&m, BTN_ACTION, 9000).kind);
    }
}

static void test_confirm_timeout_and_cancel(void)
{
    settings_menu_t m;
    settings_menu_open(&m, DEFAULTS);
    go_to(&m, SETTINGS_ROW_FACTORY_RESET);

    /* Timeout. */
    click(&m, BTN_ACTION, 100);
    settings_act_t a = settings_menu_tick(&m, 100 + SETTINGS_CONFIRM_MS);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_CANCELLED, a.kind);
    TEST_ASSERT_FALSE(m.confirming);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_tick(&m, 200 + SETTINGS_CONFIRM_MS).kind);

    /* A late second click without a tick in between asks again instead of executing. */
    click(&m, BTN_ACTION, 20000);
    a = click(&m, BTN_ACTION, 20000 + SETTINGS_CONFIRM_MS);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_ASK, a.kind);
    TEST_ASSERT_TRUE(m.confirming);

    /* Moving the cursor cancels; coming back needs two clicks again. */
    a = click(&m, BTN_REJECT, 20001 + SETTINGS_CONFIRM_MS);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_CANCELLED, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_UNPAIR, m.cursor);
    TEST_ASSERT_FALSE(m.confirming);
    click(&m, BTN_APPROVE, 30000);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_ASK, click(&m, BTN_ACTION, 30001).kind);
    a = click(&m, BTN_APPROVE, 30002);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_CANCELLED, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ROW_ABOUT, m.cursor);

    /* Exit while confirming leaves without executing. */
    click(&m, BTN_REJECT, 31000);
    click(&m, BTN_ACTION, 31001);
    a = hold(&m, BTN_ACTION, 31002);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, a.kind);
    TEST_ASSERT_FALSE(m.confirming);

    /* Across the 32-bit wrap. */
    settings_menu_open(&m, DEFAULTS);
    go_to(&m, SETTINGS_ROW_UNPAIR);
    click(&m, BTN_ACTION, UINT32_MAX - 1000);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_tick(&m, 2000).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXECUTE, click(&m, BTN_ACTION, 3000).kind);
}

/* Drives the menu with what btn_fsm makes of raw driver events, as on the device. */
static settings_act_t feed(btn_fsm_t *f, settings_menu_t *m, btn_id_t id, btn_raw_t raw, uint32_t now, bool *emitted)
{
    btn_fsm_out_t out;
    *emitted = btn_fsm_raw(f, id, raw, now, &out);
    if (!*emitted) {
        return (settings_act_t){.kind = SETTINGS_ACT_NONE};
    }
    return settings_menu_button(m, &out, now);
}

static void test_holding_action_to_exit_never_activates(void)
{
    static const settings_row_t ROWS[] = {SETTINGS_ROW_BRIGHTNESS, SETTINGS_ROW_MODE, SETTINGS_ROW_UNPAIR,
                                          SETTINGS_ROW_FACTORY_RESET, SETTINGS_ROW_ABOUT};
    for (size_t r = 0; r < sizeof(ROWS) / sizeof(ROWS[0]); r++) {
        for (int confirming = 0; confirming < 2; confirming++) {
            btn_fsm_t f;
            btn_fsm_init(&f);
            settings_menu_t m;
            settings_menu_open(&m, DEFAULTS);
            go_to(&m, ROWS[r]);
            bool emitted;
            if (confirming && settings_menu_needs_confirm(ROWS[r])) {
                feed(&f, &m, BTN_ACTION, BTN_RAW_DOWN, 0, &emitted);
                TEST_ASSERT_EQUAL(SETTINGS_ACT_CONFIRM_ASK, feed(&f, &m, BTN_ACTION, BTN_RAW_UP, 80, &emitted).kind);
            }
            const settings_menu_t before = m;

            settings_act_t a = feed(&f, &m, BTN_ACTION, BTN_RAW_DOWN, 1000, &emitted);
            TEST_ASSERT_FALSE(emitted);
            a = feed(&f, &m, BTN_ACTION, BTN_RAW_LONG_START, 2000, &emitted);
            TEST_ASSERT_TRUE(emitted);
            TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, a.kind);
            /* The release after the long press produces no click. */
            a = feed(&f, &m, BTN_ACTION, BTN_RAW_UP, 2600, &emitted);
            TEST_ASSERT_FALSE(emitted);
            TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, a.kind);
            TEST_ASSERT_EQUAL_MEMORY(before.values, m.values, sizeof(m.values));
            TEST_ASSERT_EQUAL(before.cursor, m.cursor);
            TEST_ASSERT_FALSE(m.confirming);
        }
    }

    /* A short press is a click on release and does activate. */
    btn_fsm_t f;
    btn_fsm_init(&f);
    settings_menu_t m;
    settings_menu_open(&m, DEFAULTS);
    bool emitted;
    feed(&f, &m, BTN_ACTION, BTN_RAW_DOWN, 0, &emitted);
    TEST_ASSERT_FALSE(emitted);
    const settings_act_t a = feed(&f, &m, BTN_ACTION, BTN_RAW_UP, 120, &emitted);
    TEST_ASSERT_TRUE(emitted);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_VALUE, a.kind);
    TEST_ASSERT_EQUAL_UINT16(100, a.value);

    /* The press that wakes the device is swallowed whole, long or short. */
    btn_fsm_arm_swallow(&f, 5000);
    feed(&f, &m, BTN_ACTION, BTN_RAW_DOWN, 5000, &emitted);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, feed(&f, &m, BTN_ACTION, BTN_RAW_LONG_START, 6000, &emitted).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, feed(&f, &m, BTN_ACTION, BTN_RAW_UP, 6100, &emitted).kind);
    TEST_ASSERT_EQUAL_UINT16(100, m.values[SETTINGS_ROW_BRIGHTNESS]);
}

static void test_texts(void)
{
    char t[SETTINGS_VALUE_TEXT_SIZE];
    settings_menu_value_text(SETTINGS_ROW_BRIGHTNESS, 75, t);
    TEST_ASSERT_EQUAL_STRING("75 %", t);
    settings_menu_value_text(SETTINGS_ROW_SCREEN_TIMEOUT, 120, t);
    TEST_ASSERT_EQUAL_STRING("120 s", t);
    settings_menu_value_text(SETTINGS_ROW_DEEP_SLEEP, 0, t);
    TEST_ASSERT_EQUAL_STRING("Never", t);
    settings_menu_value_text(SETTINGS_ROW_DEEP_SLEEP, 20, t);
    TEST_ASSERT_EQUAL_STRING("20 min", t);
    settings_menu_value_text(SETTINGS_ROW_HAPTICS, 1, t);
    TEST_ASSERT_EQUAL_STRING("Gentle", t);
    settings_menu_value_text(SETTINGS_ROW_HAPTICS, 9, t);
    TEST_ASSERT_EQUAL_STRING("?", t);
    settings_menu_value_text(SETTINGS_ROW_LED, 0, t);
    TEST_ASSERT_EQUAL_STRING("Off", t);
    settings_menu_value_text(SETTINGS_ROW_MODE, SETTINGS_MODE_DESK, t);
    TEST_ASSERT_EQUAL_STRING("Desk", t);
    settings_menu_value_text(SETTINGS_ROW_UNPAIR, 1, t);
    TEST_ASSERT_EQUAL_STRING("", t);
    settings_menu_value_text(SETTINGS_ROW_BRIGHTNESS, 65535, t);
    TEST_ASSERT_EQUAL_STRING("65535 %", t);
    for (int r = 0; r < SETTINGS_ROW_COUNT; r++) {
        TEST_ASSERT_TRUE(settings_menu_label((settings_row_t)r)[0] != '\0');
        TEST_ASSERT_TRUE(settings_menu_key((settings_row_t)r)[0] != '\0');
        size_t n;
        const uint16_t *c = settings_menu_choices((settings_row_t)r, &n);
        TEST_ASSERT_EQUAL(r < SETTINGS_VALUE_ROWS, c != NULL);
        TEST_ASSERT_EQUAL(r < SETTINGS_VALUE_ROWS, n > 0);
    }
    TEST_ASSERT_EQUAL_STRING("", settings_menu_label(SETTINGS_ROW_COUNT));
    TEST_ASSERT_FALSE(settings_menu_allowed(SETTINGS_ROW_BRIGHTNESS, 30));
    TEST_ASSERT_FALSE(settings_menu_allowed(SETTINGS_ROW_ABOUT, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_and_navigation);
    RUN_TEST(test_value_cycling);
    RUN_TEST(test_about_executes_at_once);
    RUN_TEST(test_confirm_twice);
    RUN_TEST(test_confirm_timeout_and_cancel);
    RUN_TEST(test_holding_action_to_exit_never_activates);
    RUN_TEST(test_texts);
    return UNITY_END();
}
