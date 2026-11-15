#include "settings_menu.h"

#include "btn_fsm.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define V(item) ((item) - SETTINGS_VALUE_FIRST)

static const uint16_t DEFAULTS[SETTINGS_VALUE_COUNT] = {
    [V(SETTINGS_ITEM_BRIGHTNESS)] = 75, [V(SETTINGS_ITEM_SCREEN_TIMEOUT)] = 30, [V(SETTINGS_ITEM_DEEP_SLEEP)] = 20,
    [V(SETTINGS_ITEM_SHOW_CLOCK)] = 0,  [V(SETTINGS_ITEM_CLOCK_BRIGHTNESS)] = 30, [V(SETTINGS_ITEM_H24)] = 1,
    [V(SETTINGS_ITEM_NIGHT)] = 1,       [V(SETTINGS_ITEM_HAPTICS)] = 2,         [V(SETTINGS_ITEM_LED)] = 1,
};

static settings_act_t press(settings_menu_t *m, btn_id_t id, btn_kind_t kind, uint32_t now)
{
    const btn_fsm_out_t out = {.id = id, .kind = kind, .permille = 500};
    return settings_menu_button(m, &out, now);
}

static settings_act_t click(settings_menu_t *m, btn_id_t id, uint32_t now)
{
    return press(m, id, BTN_CLICK, now);
}

static void go_to(settings_menu_t *m, settings_item_t item)
{
    for (int i = 0; i < SETTINGS_LIST_MAX && settings_menu_item(m) != item; i++) {
        click(m, BTN_ACTION, 0);
    }
    TEST_ASSERT_EQUAL(item, settings_menu_item(m));
}

static void test_top_list_order_and_wrap(void)
{
    static const settings_item_t EXPECTED[] = {
        SETTINGS_ITEM_PET,  SETTINGS_ITEM_DISPLAY, SETTINGS_ITEM_CLOCK,    SETTINGS_ITEM_HAPTICS_LED,
        SETTINGS_ITEM_DESK, SETTINGS_ITEM_NETWORK, SETTINGS_ITEM_SHOWCASE, SETTINGS_ITEM_ABOUT,
    };
    size_t n;
    const settings_item_t *items = settings_list_items(SETTINGS_LIST_TOP, &n);
    TEST_ASSERT_EQUAL(8, n);
    TEST_ASSERT_EQUAL_MEMORY(EXPECTED, items, sizeof(EXPECTED));
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_TOP, DEFAULTS, false, 0);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL(EXPECTED[i], settings_menu_item(&m));
        TEST_ASSERT_EQUAL(SETTINGS_ACT_MOVED, click(&m, BTN_ACTION, 0).kind);
    }
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_PET, settings_menu_item(&m));
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, click(&m, BTN_REJECT, 0).kind);
}

static void test_clock_sub_list(void)
{
    static const settings_item_t EXPECTED[] = {SETTINGS_ITEM_SHOW_CLOCK, SETTINGS_ITEM_CLOCK_BRIGHTNESS,
                                               SETTINGS_ITEM_H24, SETTINGS_ITEM_NIGHT};
    size_t n;
    const settings_item_t *items = settings_list_items(SETTINGS_LIST_CLOCK, &n);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_MEMORY(EXPECTED, items, sizeof(EXPECTED));
}

static void test_sub_list_back_lands_on_its_row(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_TOP, DEFAULTS, false, 0);
    go_to(&m, SETTINGS_ITEM_NETWORK);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_MOVED, click(&m, BTN_APPROVE, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_LIST_NETWORK, m.list);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_WIFI_SETUP, settings_menu_item(&m));
    click(&m, BTN_REJECT, 0);
    TEST_ASSERT_EQUAL(SETTINGS_LIST_TOP, m.list);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_NETWORK, settings_menu_item(&m));
}

static void test_editor_preview_cancel_restores(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_DISPLAY, DEFAULTS, false, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_MOVED, click(&m, BTN_APPROVE, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_EDITOR, m.view);
    settings_act_t a = click(&m, BTN_ACTION, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_PREVIEW, a.kind);
    TEST_ASSERT_EQUAL(100, a.value);
    a = click(&m, BTN_ACTION, 0);
    TEST_ASSERT_EQUAL(25, a.value);
    TEST_ASSERT_EQUAL(25, settings_menu_value(&m, SETTINGS_ITEM_BRIGHTNESS));
    a = click(&m, BTN_REJECT, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CANCEL, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_BRIGHTNESS, a.item);
    TEST_ASSERT_EQUAL(75, a.value);
    TEST_ASSERT_EQUAL(75, settings_menu_value(&m, SETTINGS_ITEM_BRIGHTNESS));
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_LIST, m.view);
}

static void test_editor_save(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_DISPLAY, DEFAULTS, false, 0);
    go_to(&m, SETTINGS_ITEM_SCREEN_TIMEOUT);
    click(&m, BTN_APPROVE, 0);
    click(&m, BTN_ACTION, 0);
    click(&m, BTN_ACTION, 0);
    click(&m, BTN_ACTION, 0);
    const settings_act_t a = click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_SAVE, a.kind);
    TEST_ASSERT_EQUAL(0, a.value);
    char text[SETTINGS_VALUE_TEXT_SIZE];
    settings_value_text(SETTINGS_ITEM_SCREEN_TIMEOUT, a.value, text);
    TEST_ASSERT_EQUAL_STRING("Never", text);
}

static void test_editor_timeout_cancels(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_CLOCK, DEFAULTS, false, 1000);
    click(&m, BTN_APPROVE, 1000);
    click(&m, BTN_ACTION, 2000);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_tick(&m, 2000 + SETTINGS_EDITOR_IDLE_MS - 1).kind);
    const settings_act_t a = settings_menu_tick(&m, 2000 + SETTINGS_EDITOR_IDLE_MS);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_CANCEL, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_CLOCK_RESTING, a.value);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_LIST, m.view);
    /* The list's own minute starts again from the cancel. */
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, settings_menu_tick(&m, 2000 + SETTINGS_EDITOR_IDLE_MS +
                                                                      SETTINGS_LIST_IDLE_MS).kind);
}

static void test_list_timeout_exits(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_QUICK, DEFAULTS, false, 500);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_tick(&m, 500 + SETTINGS_LIST_IDLE_MS - 1).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, settings_menu_tick(&m, 500 + SETTINGS_LIST_IDLE_MS).kind);
}

static void test_destructive_needs_commit_hold(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_NETWORK, DEFAULTS, false, 0);
    go_to(&m, SETTINGS_ITEM_FACTORY_RESET);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_MOVED, click(&m, BTN_APPROVE, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_CONFIRM, m.view);
    TEST_ASSERT_EQUAL(BTN_MODE_COMMIT_HOLD, settings_menu_btn_mode(&m, BTN_APPROVE));
    TEST_ASSERT_EQUAL(BTN_MODE_NONE, settings_menu_btn_mode(&m, BTN_ACTION));
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, click(&m, BTN_ACTION, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_PROGRESS, press(&m, BTN_APPROVE, BTN_PROGRESS, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NUDGE, press(&m, BTN_APPROVE, BTN_CANCELLED, 0).kind);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_CONFIRM, m.view);
    const settings_act_t a = press(&m, BTN_APPROVE, BTN_COMMIT, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXECUTE, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_FACTORY_RESET, a.item);
}

static void test_confirm_cancel_and_timeout(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_NETWORK, DEFAULTS, false, 0);
    go_to(&m, SETTINGS_ITEM_UNPAIR);
    click(&m, BTN_APPROVE, 0);
    click(&m, BTN_REJECT, 0);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_LIST, m.view);
    TEST_ASSERT_EQUAL(SETTINGS_LIST_NETWORK, m.list);
    click(&m, BTN_APPROVE, 100);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_tick(&m, 100 + SETTINGS_CONFIRM_IDLE_MS - 1).kind);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_MOVED, settings_menu_tick(&m, 100 + SETTINGS_CONFIRM_IDLE_MS).kind);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_LIST, m.view);
}

static void test_wifi_setup_is_a_click(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_NETWORK, DEFAULTS, false, 0);
    const settings_act_t a = click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXECUTE, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_WIFI_SETUP, a.item);
}

static void test_quick_menu(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_QUICK, DEFAULTS, true, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_SETTINGS, settings_menu_item(&m));
    go_to(&m, SETTINGS_ITEM_DESK);
    settings_act_t a = click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_DESK, a.kind);
    TEST_ASSERT_EQUAL(0, a.value);
    go_to(&m, SETTINGS_ITEM_SETTINGS);
    click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_LIST_TOP, m.list);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_PET, settings_menu_item(&m));
    /* Back from the top list leaves the menu, not to the quick menu. */
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXIT, click(&m, BTN_REJECT, 0).kind);
}

static void test_showcase_refused_while_blocked(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_QUICK, DEFAULTS, false, 0);
    go_to(&m, SETTINGS_ITEM_SHOWCASE);
    settings_menu_sync(&m, false, true);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_REFUSED, click(&m, BTN_APPROVE, 0).kind);
    settings_menu_sync(&m, false, false);
    const settings_act_t a = click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(SETTINGS_ACT_EXECUTE, a.kind);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_SHOWCASE, a.item);
}

static void test_swallowed_press_does_nothing(void)
{
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_TOP, DEFAULTS, false, 0);
    const btn_fsm_out_t out = {.id = BTN_REJECT, .kind = BTN_CLICK, .swallowed = true};
    TEST_ASSERT_EQUAL(SETTINGS_ACT_NONE, settings_menu_button(&m, &out, 0).kind);
}

static void test_open_path(void)
{
    settings_menu_t m;
    TEST_ASSERT_TRUE(settings_menu_open_path(&m, "clock/clock_brightness", DEFAULTS, false, 0));
    TEST_ASSERT_EQUAL(SETTINGS_LIST_CLOCK, m.list);
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_EDITOR, m.view);
    TEST_ASSERT_EQUAL(SETTINGS_ITEM_CLOCK_BRIGHTNESS, settings_menu_item(&m));
    TEST_ASSERT_TRUE(settings_menu_open_path(&m, "network/factory_reset", DEFAULTS, false, 0));
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_CONFIRM, m.view);
    TEST_ASSERT_TRUE(settings_menu_open_path(&m, "top", DEFAULTS, false, 0));
    TEST_ASSERT_EQUAL(SETTINGS_VIEW_LIST, m.view);
    TEST_ASSERT_FALSE(settings_menu_open_path(&m, "nope", DEFAULTS, false, 0));
    TEST_ASSERT_FALSE(settings_menu_open_path(&m, "clock/brightness", DEFAULTS, false, 0));
    TEST_ASSERT_EQUAL(SETTINGS_LIST_TOP, m.list);
}

static void test_value_texts(void)
{
    char num[SETTINGS_VALUE_TEXT_SIZE];
    char unit[SETTINGS_VALUE_TEXT_SIZE];
    char text[SETTINGS_VALUE_TEXT_SIZE];
    settings_value_parts(SETTINGS_ITEM_BRIGHTNESS, 75, num, unit);
    TEST_ASSERT_EQUAL_STRING("75", num);
    TEST_ASSERT_EQUAL_STRING("%", unit);
    settings_value_text(SETTINGS_ITEM_BRIGHTNESS, 75, text);
    TEST_ASSERT_EQUAL_STRING("75 %", text);
    settings_value_text(SETTINGS_ITEM_DEEP_SLEEP, 20, text);
    TEST_ASSERT_EQUAL_STRING("20 min", text);
    settings_value_text(SETTINGS_ITEM_SHOW_CLOCK, SETTINGS_CLOCK_ALWAYS_USB, text);
    TEST_ASSERT_EQUAL_STRING("Always on USB", text);
    settings_value_text(SETTINGS_ITEM_H24, 0, text);
    TEST_ASSERT_EQUAL_STRING("12 h", text);
    settings_value_text(SETTINGS_ITEM_HAPTICS, 1, text);
    TEST_ASSERT_EQUAL_STRING("Gentle", text);
    settings_value_text(SETTINGS_ITEM_DESK, 0, text);
    TEST_ASSERT_EQUAL_STRING("Off", text);
}

static void test_choices_include_defaults(void)
{
    for (settings_item_t i = SETTINGS_VALUE_FIRST; i < SETTINGS_VALUE_FIRST + SETTINGS_VALUE_COUNT; i++) {
        TEST_ASSERT_TRUE_MESSAGE(settings_item_allowed(i, DEFAULTS[V(i)]), settings_item_key(i));
    }
    TEST_ASSERT_FALSE(settings_item_allowed(SETTINGS_ITEM_PET, 0));
    TEST_ASSERT_FALSE(settings_item_allowed(SETTINGS_ITEM_CLOCK_BRIGHTNESS, 31));
}

static void test_unknown_stored_value_starts_cycle(void)
{
    uint16_t values[SETTINGS_VALUE_COUNT];
    memcpy(values, DEFAULTS, sizeof(values));
    values[V(SETTINGS_ITEM_BRIGHTNESS)] = 33;
    settings_menu_t m;
    settings_menu_open(&m, SETTINGS_LIST_DISPLAY, values, false, 0);
    click(&m, BTN_APPROVE, 0);
    TEST_ASSERT_EQUAL(25, click(&m, BTN_ACTION, 0).value);
    TEST_ASSERT_EQUAL(33, click(&m, BTN_REJECT, 0).value);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_top_list_order_and_wrap);
    RUN_TEST(test_clock_sub_list);
    RUN_TEST(test_sub_list_back_lands_on_its_row);
    RUN_TEST(test_editor_preview_cancel_restores);
    RUN_TEST(test_editor_save);
    RUN_TEST(test_editor_timeout_cancels);
    RUN_TEST(test_list_timeout_exits);
    RUN_TEST(test_destructive_needs_commit_hold);
    RUN_TEST(test_confirm_cancel_and_timeout);
    RUN_TEST(test_wifi_setup_is_a_click);
    RUN_TEST(test_quick_menu);
    RUN_TEST(test_showcase_refused_while_blocked);
    RUN_TEST(test_swallowed_press_does_nothing);
    RUN_TEST(test_open_path);
    RUN_TEST(test_value_texts);
    RUN_TEST(test_choices_include_defaults);
    RUN_TEST(test_unknown_stored_value_starts_cycle);
    return UNITY_END();
}
