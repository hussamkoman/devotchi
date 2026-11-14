#include "battery.h"

#include "unity.h"

#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

#define LOW_MV   3500
#define CRIT_MV  3300

static battery_t s_b;
static battery_output_t s_out;

static void update(int32_t mv, bool chrg, bool stdby, uint16_t edges, battery_presence_t presence, uint32_t now)
{
    const battery_input_t in = {
        .mv = mv,
        .chrg_low = chrg,
        .stdby_low = stdby,
        .chrg_edges_10s = edges,
        .presence = presence,
        .now_ms = now,
    };
    battery_update(&s_b, &in, &s_out);
}

static void test_table_points(void)
{
    static const struct {
        int32_t mv;
        int pct;
    } points[] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3950, 75}, {3900, 68}, {3850, 60},
        {3800, 50},  {3750, 38}, {3700, 25}, {3600, 12}, {3500, 5},  {3300, 0},
    };
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        char msg[16];
        snprintf(msg, sizeof(msg), "%d mV", (int)points[i].mv);
        TEST_ASSERT_EQUAL_INT_MESSAGE(points[i].pct, battery_pct_from_mv(points[i].mv), msg);
    }
}

static void test_interpolation_and_clamp(void)
{
    TEST_ASSERT_EQUAL_INT(95, battery_pct_from_mv(4150));
    TEST_ASSERT_EQUAL_INT(44, battery_pct_from_mv(3775));
    TEST_ASSERT_EQUAL_INT(18, battery_pct_from_mv(3650));
    TEST_ASSERT_EQUAL_INT(2, battery_pct_from_mv(3400));
    TEST_ASSERT_EQUAL_INT(71, battery_pct_from_mv(3925));
    TEST_ASSERT_EQUAL_INT(100, battery_pct_from_mv(4500));
    TEST_ASSERT_EQUAL_INT(0, battery_pct_from_mv(3299));
    TEST_ASSERT_EQUAL_INT(0, battery_pct_from_mv(0));
    TEST_ASSERT_EQUAL_INT(0, battery_pct_from_mv(-100));
    /* Monotonic over the whole range. */
    int last = 0;
    for (int32_t mv = 3000; mv <= 4400; mv++) {
        const int p = battery_pct_from_mv(mv);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(last, p);
        last = p;
    }
}

static void test_median5(void)
{
    const int32_t a[] = {4100, 4102, 9999, 4098, 4101};
    TEST_ASSERT_EQUAL_INT32(4101, battery_median5(a));
    const int32_t b[] = {5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32(3, battery_median5(b));
    const int32_t c[] = {0, 0, 4000, 4000, 4000};
    TEST_ASSERT_EQUAL_INT32(4000, battery_median5(c));
}

static void test_filter_rejects_outlier(void)
{
    battery_filter_t f;
    battery_filter_init(&f);
    TEST_ASSERT_EQUAL_INT32(4000, battery_filter_add(&f, 4000));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT32(4000, battery_filter_add(&f, 4000));
    }
    /* One spike or one dip in a full window does not move the output. */
    TEST_ASSERT_EQUAL_INT32(4000, battery_filter_add(&f, 2500));
    TEST_ASSERT_EQUAL_INT32(4000, battery_filter_add(&f, 4000));
    TEST_ASSERT_EQUAL_INT32(4000, battery_filter_add(&f, 6000));
    TEST_ASSERT_EQUAL_INT32(4000, battery_filter_add(&f, 4000));
}

static void test_filter_settles(void)
{
    battery_filter_t f;
    battery_filter_init(&f);
    for (int i = 0; i < 5; i++) {
        battery_filter_add(&f, 4000);
    }
    /* A step: the median follows after three samples, the IIR closes 1/4 of the gap per sample. */
    int32_t v = 0;
    int32_t prev = 4000;
    for (int i = 0; i < 40; i++) {
        v = battery_filter_add(&f, 3800);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(prev, v);
        prev = v;
        if (i == 1) {
            TEST_ASSERT_EQUAL_INT32(4000, v);
        }
        if (i == 2) {
            TEST_ASSERT_EQUAL_INT32(3950, v);
        }
    }
    TEST_ASSERT_EQUAL_INT32(3800, v);
}

static void test_ext_power_debounce(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, 0);
    TEST_ASSERT_FALSE(s_out.ext_power);
    TEST_ASSERT_FALSE(s_out.ext_power_changed);

    update(4000, true, false, 0, BATTERY_PRESENCE_YES, 1000);
    TEST_ASSERT_TRUE(s_out.ext_power);
    TEST_ASSERT_TRUE(s_out.ext_power_changed);

    /* CHRG high again: still external power for 2 s. */
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, 2000);
    TEST_ASSERT_TRUE(s_out.ext_power);
    TEST_ASSERT_FALSE(s_out.ext_power_changed);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, 3000);
    TEST_ASSERT_TRUE(s_out.ext_power);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, 3001);
    TEST_ASSERT_FALSE(s_out.ext_power);
    TEST_ASSERT_TRUE(s_out.ext_power_changed);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, 4000);
    TEST_ASSERT_FALSE(s_out.ext_power_changed);

    /* STDBY alone is external power too. */
    update(4180, false, true, 0, BATTERY_PRESENCE_YES, 5000);
    TEST_ASSERT_TRUE(s_out.ext_power);
    TEST_ASSERT_TRUE(s_out.ext_power_changed);
    TEST_ASSERT_EQUAL(BATTERY_STATE_FULL, s_out.state);
}

static void test_states(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(3900, false, false, 0, BATTERY_PRESENCE_AUTO, 0);
    TEST_ASSERT_EQUAL(BATTERY_STATE_DISCHARGING, s_out.state);
    TEST_ASSERT_EQUAL_INT(68, s_out.pct);

    /* Charging reads the table 100 mV lower. */
    update(4000, true, false, 0, BATTERY_PRESENCE_AUTO, 1000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    TEST_ASSERT_EQUAL_INT(68, s_out.pct);

    update(4190, false, true, 0, BATTERY_PRESENCE_AUTO, 2000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_FULL, s_out.state);
    TEST_ASSERT_EQUAL_INT(100, s_out.pct);
    TEST_ASSERT_FALSE(s_out.low);
    TEST_ASSERT_FALSE(s_out.critical);

    update(4100, true, false, 5, BATTERY_PRESENCE_AUTO, 3000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_ABSENT, s_out.state);
    TEST_ASSERT_EQUAL_INT(-1, s_out.pct);
    TEST_ASSERT_TRUE(s_out.ext_power);

    /* Absent setting wins over everything, also without external power. */
    update(3900, false, false, 0, BATTERY_PRESENCE_NO, 10000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_ABSENT, s_out.state);
    TEST_ASSERT_FALSE(s_out.ext_power);
    TEST_ASSERT_FALSE(s_out.low);
    update(3200, false, false, 0, BATTERY_PRESENCE_NO, 50000);
    TEST_ASSERT_FALSE(s_out.low);
    TEST_ASSERT_FALSE(s_out.critical);
}

static void test_absent_needs_auto_and_ext_power(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(4100, true, false, 2, BATTERY_PRESENCE_AUTO, 0);
    TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    update(4100, true, false, 3, BATTERY_PRESENCE_AUTO, 1000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_ABSENT, s_out.state);
    /* Present setting overrides the heuristic. */
    update(4100, true, false, 20, BATTERY_PRESENCE_YES, 2000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    /* Edges without external power (impossible in practice) do not mean absent. */
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(3900, false, false, 9, BATTERY_PRESENCE_AUTO, 0);
    TEST_ASSERT_EQUAL(BATTERY_STATE_DISCHARGING, s_out.state);
}

static void test_full_by_voltage_hold(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    uint32_t t = 0;
    update(4160, true, false, 0, BATTERY_PRESENCE_YES, t);
    TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    for (t = 1000; t < BATTERY_FULL_HOLD_MS; t += 1000) {
        update(4160, true, false, 0, BATTERY_PRESENCE_YES, t);
        TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    }
    update(4160, true, false, 0, BATTERY_PRESENCE_YES, BATTERY_FULL_HOLD_MS);
    TEST_ASSERT_EQUAL(BATTERY_STATE_FULL, s_out.state);
    TEST_ASSERT_EQUAL_INT(100, s_out.pct);

    /* One dip below 4150 restarts the 10 minutes. */
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(4160, true, false, 0, BATTERY_PRESENCE_YES, 0);
    update(4140, true, false, 0, BATTERY_PRESENCE_YES, 300000);
    update(4160, true, false, 0, BATTERY_PRESENCE_YES, 301000);
    update(4160, true, false, 0, BATTERY_PRESENCE_YES, BATTERY_FULL_HOLD_MS + 1000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    update(4160, true, false, 0, BATTERY_PRESENCE_YES, 301000 + BATTERY_FULL_HOLD_MS);
    TEST_ASSERT_EQUAL(BATTERY_STATE_FULL, s_out.state);
}

static void test_charging_never_decreases(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(3900, true, false, 0, BATTERY_PRESENCE_YES, 0);
    TEST_ASSERT_EQUAL_INT(50, s_out.pct);
    update(3950, true, false, 0, BATTERY_PRESENCE_YES, 1000);
    TEST_ASSERT_EQUAL_INT(60, s_out.pct);
    update(3850, true, false, 0, BATTERY_PRESENCE_YES, 2000);
    TEST_ASSERT_EQUAL_INT(60, s_out.pct);
    /* Capped at 99 until full. */
    update(4400, true, false, 0, BATTERY_PRESENCE_YES, 3000);
    TEST_ASSERT_EQUAL_INT(99, s_out.pct);
    TEST_ASSERT_FALSE(s_out.low);
}

static void test_discharging_never_increases(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(3800, false, false, 0, BATTERY_PRESENCE_YES, 0);
    TEST_ASSERT_EQUAL_INT(50, s_out.pct);
    update(3900, false, false, 0, BATTERY_PRESENCE_YES, 1000);
    TEST_ASSERT_EQUAL_INT(50, s_out.pct);
    update(3700, false, false, 0, BATTERY_PRESENCE_YES, 2000);
    TEST_ASSERT_EQUAL_INT(25, s_out.pct);
    update(3750, false, false, 0, BATTERY_PRESENCE_YES, 3000);
    TEST_ASSERT_EQUAL_INT(25, s_out.pct);
}

static void test_monotonic_memory_resets_on_class_change(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(3700, false, false, 0, BATTERY_PRESENCE_YES, 0);
    TEST_ASSERT_EQUAL_INT(25, s_out.pct);
    /* Plugged in: charging starts from the table, not from the discharge memory. */
    update(3900, true, false, 0, BATTERY_PRESENCE_YES, 1000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
    TEST_ASSERT_EQUAL_INT(50, s_out.pct);
    update(4000, true, false, 0, BATTERY_PRESENCE_YES, 2000);
    TEST_ASSERT_EQUAL_INT(68, s_out.pct);
    /* Unplugged (after the debounce): discharging restarts from the table. */
    update(3900, false, false, 0, BATTERY_PRESENCE_YES, 5000);
    TEST_ASSERT_EQUAL(BATTERY_STATE_DISCHARGING, s_out.state);
    TEST_ASSERT_EQUAL_INT(68, s_out.pct);
}

static void test_low_and_critical(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(3510, false, false, 0, BATTERY_PRESENCE_YES, 0);
    TEST_ASSERT_FALSE(s_out.low);
    update(3499, false, false, 0, BATTERY_PRESENCE_YES, 1000);
    TEST_ASSERT_TRUE(s_out.low);
    TEST_ASSERT_FALSE(s_out.critical);

    update(3290, false, false, 0, BATTERY_PRESENCE_YES, 2000);
    TEST_ASSERT_TRUE(s_out.low);
    TEST_ASSERT_FALSE(s_out.critical);
    update(3290, false, false, 0, BATTERY_PRESENCE_YES, 2000 + BATTERY_CRIT_HOLD_MS - 1);
    TEST_ASSERT_FALSE(s_out.critical);
    update(3290, false, false, 0, BATTERY_PRESENCE_YES, 2000 + BATTERY_CRIT_HOLD_MS);
    TEST_ASSERT_TRUE(s_out.critical);

    /* A recovery above the threshold restarts the 30 s. */
    update(3310, false, false, 0, BATTERY_PRESENCE_YES, 40000);
    TEST_ASSERT_FALSE(s_out.critical);
    update(3290, false, false, 0, BATTERY_PRESENCE_YES, 41000);
    update(3290, false, false, 0, BATTERY_PRESENCE_YES, 70000);
    TEST_ASSERT_FALSE(s_out.critical);
    update(3290, false, false, 0, BATTERY_PRESENCE_YES, 71000);
    TEST_ASSERT_TRUE(s_out.critical);

    /* Never low or critical with external power. */
    update(3290, true, false, 0, BATTERY_PRESENCE_YES, 72000);
    TEST_ASSERT_FALSE(s_out.low);
    TEST_ASSERT_FALSE(s_out.critical);
}

static void test_reset_charge_memory(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    update(4100, true, false, 0, BATTERY_PRESENCE_YES, 0);
    update(3200, false, false, 0, BATTERY_PRESENCE_YES, 5000);
    update(3200, false, false, 0, BATTERY_PRESENCE_YES, 5000 + BATTERY_CRIT_HOLD_MS);
    TEST_ASSERT_TRUE(s_out.critical);
    TEST_ASSERT_EQUAL_INT(0, s_out.pct);

    battery_reset_charge_memory(&s_b);
    update(3900, false, false, 0, BATTERY_PRESENCE_YES, 40000);
    TEST_ASSERT_EQUAL_INT(68, s_out.pct);
    TEST_ASSERT_FALSE(s_out.critical);
    TEST_ASSERT_FALSE(s_out.ext_power_changed);

    /* The debounce survives: external power seen just before the reset is still held. */
    update(4000, true, false, 0, BATTERY_PRESENCE_YES, 50000);
    TEST_ASSERT_TRUE(s_out.ext_power_changed);
    battery_reset_charge_memory(&s_b);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, 51000);
    TEST_ASSERT_TRUE(s_out.ext_power);
    TEST_ASSERT_FALSE(s_out.ext_power_changed);
}

static void test_time_wraparound(void)
{
    battery_init(&s_b, LOW_MV, CRIT_MV);
    const uint32_t t0 = UINT32_MAX - 1000;
    update(4000, true, false, 0, BATTERY_PRESENCE_YES, t0);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, t0 + 1500);
    TEST_ASSERT_TRUE(s_out.ext_power);
    update(4000, false, false, 0, BATTERY_PRESENCE_YES, t0 + 2500);
    TEST_ASSERT_FALSE(s_out.ext_power);

    update(3200, false, false, 0, BATTERY_PRESENCE_YES, t0 + 3000);
    update(3200, false, false, 0, BATTERY_PRESENCE_YES, t0 + 3000 + BATTERY_CRIT_HOLD_MS);
    TEST_ASSERT_TRUE(s_out.critical);
}

/*
 * A charger without a cell toggling CHRG, sampled as the pwr task does: the
 * level once per second plus the interrupt's edge count for that second.
 */
typedef struct {
    uint32_t period_ms;
    uint32_t low_ms;
    uint32_t phase_ms;
} wave_t;

static bool wave_low(const wave_t *w, uint32_t t)
{
    return ((t + w->phase_ms) % w->period_ms) < w->low_ms;
}

static void run_toggling(const wave_t *w, battery_presence_t presence, battery_state_t expect_state)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "period %u low %u phase %u", (unsigned)w->period_ms, (unsigned)w->low_ms,
             (unsigned)w->phase_ms);

    battery_init(&s_b, LOW_MV, CRIT_MV);
    uint16_t ring[10] = {0};
    int changes = 0;
    bool prev = wave_low(w, 0);
    for (uint32_t second = 1; second <= 60; second++) {
        uint16_t edges = 0;
        for (uint32_t t = (second - 1) * 1000 + 1; t <= second * 1000; t++) {
            const bool level = wave_low(w, t);
            if (level != prev) {
                edges++;
                prev = level;
            }
        }
        ring[second % 10] = edges;
        uint16_t sum = 0;
        for (int i = 0; i < 10; i++) {
            sum += ring[i];
        }
        const bool chrg_low = wave_low(w, second * 1000) || edges > 0;
        update(4100, chrg_low, false, sum, presence, second * 1000);
        TEST_ASSERT_TRUE_MESSAGE(s_out.ext_power, msg);
        if (s_out.ext_power_changed) {
            changes++;
        }
        if (second >= 3) {
            TEST_ASSERT_EQUAL_MESSAGE(expect_state, s_out.state, msg);
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1, changes, msg);
}

static void test_chrg_toggling_keeps_ext_power(void)
{
    static const wave_t waves[] = {
        /* 1 Hz, sampled always in the high half. */
        {1000, 500, 0},
        {1000, 500, 500},
        /* 1 Hz short low pulses. */
        {1000, 50, 0},
        {1000, 50, 900},
        /* 5 Hz. */
        {200, 100, 0},
        {200, 100, 50},
        {200, 20, 0},
        /* 10 Hz. */
        {100, 50, 0},
        {100, 50, 25},
        {100, 10, 0},
    };
    for (size_t i = 0; i < sizeof(waves) / sizeof(waves[0]); i++) {
        run_toggling(&waves[i], BATTERY_PRESENCE_AUTO, BATTERY_STATE_ABSENT);
        run_toggling(&waves[i], BATTERY_PRESENCE_YES, BATTERY_STATE_CHARGING);
    }
}

static void test_steady_chrg_on_bench(void)
{
    /* The bench board: CHRG steadily low at about 4.1 V and no toggling looks like a cell on charge. */
    battery_init(&s_b, LOW_MV, CRIT_MV);
    for (uint32_t t = 0; t <= 60000; t += 1000) {
        update(4100, true, false, 0, BATTERY_PRESENCE_AUTO, t);
        TEST_ASSERT_EQUAL(BATTERY_STATE_CHARGING, s_out.state);
        TEST_ASSERT_TRUE(s_out.ext_power);
        TEST_ASSERT_EQUAL(t == 0, s_out.ext_power_changed);
    }
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("absent", battery_state_name(BATTERY_STATE_ABSENT));
    TEST_ASSERT_EQUAL_STRING("full", battery_state_name(BATTERY_STATE_FULL));
    TEST_ASSERT_EQUAL_STRING("charging", battery_state_name(BATTERY_STATE_CHARGING));
    TEST_ASSERT_EQUAL_STRING("discharging", battery_state_name(BATTERY_STATE_DISCHARGING));
    TEST_ASSERT_EQUAL_STRING("auto", battery_presence_name(BATTERY_PRESENCE_AUTO));
    TEST_ASSERT_EQUAL_STRING("yes", battery_presence_name(BATTERY_PRESENCE_YES));
    TEST_ASSERT_EQUAL_STRING("no", battery_presence_name(BATTERY_PRESENCE_NO));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_points);
    RUN_TEST(test_interpolation_and_clamp);
    RUN_TEST(test_median5);
    RUN_TEST(test_filter_rejects_outlier);
    RUN_TEST(test_filter_settles);
    RUN_TEST(test_ext_power_debounce);
    RUN_TEST(test_states);
    RUN_TEST(test_absent_needs_auto_and_ext_power);
    RUN_TEST(test_full_by_voltage_hold);
    RUN_TEST(test_charging_never_decreases);
    RUN_TEST(test_discharging_never_increases);
    RUN_TEST(test_monotonic_memory_resets_on_class_change);
    RUN_TEST(test_low_and_critical);
    RUN_TEST(test_reset_charge_memory);
    RUN_TEST(test_time_wraparound);
    RUN_TEST(test_chrg_toggling_keeps_ext_power);
    RUN_TEST(test_steady_chrg_on_bench);
    RUN_TEST(test_names);
    return UNITY_END();
}
