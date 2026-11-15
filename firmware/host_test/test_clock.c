#include "clock.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* 2026-11-15T14:32:10Z, a Sunday. */
#define T_SUN_1432  1794753130000LL

static void test_valid(void)
{
    TEST_ASSERT_FALSE(clock_valid(0));
    TEST_ASSERT_FALSE(clock_valid(CLOCK_VALID_AFTER_MS - 1));
    TEST_ASSERT_TRUE(clock_valid(CLOCK_VALID_AFTER_MS));
    TEST_ASSERT_TRUE(clock_valid(T_SUN_1432));
    TEST_ASSERT_TRUE(clock_offset_ok(-720));
    TEST_ASSERT_TRUE(clock_offset_ok(840));
    TEST_ASSERT_FALSE(clock_offset_ok(-721));
    TEST_ASSERT_FALSE(clock_offset_ok(841));
}

static void test_local_and_format(void)
{
    clock_tm_t t;
    char time[CLOCK_TIME_SIZE];
    char date[CLOCK_DATE_SIZE];
    clock_local(T_SUN_1432, 0, &t);
    TEST_ASSERT_EQUAL(2026, t.year);
    TEST_ASSERT_EQUAL(10, t.month);
    TEST_ASSERT_EQUAL(15, t.mday);
    TEST_ASSERT_EQUAL(0, t.wday);
    TEST_ASSERT_EQUAL(14, t.hour);
    TEST_ASSERT_EQUAL(32, t.minute);
    TEST_ASSERT_EQUAL(10, t.second);
    clock_format_time(&t, true, time);
    TEST_ASSERT_EQUAL_STRING("14:32", time);
    clock_format_time(&t, false, time);
    TEST_ASSERT_EQUAL_STRING("2:32", time);
    clock_format_date(&t, date);
    TEST_ASSERT_EQUAL_STRING("SUN 15 NOV", date);
    TEST_ASSERT_FALSE(clock_is_night(&t));

    /* +10:30 crosses midnight into Monday. */
    clock_local(T_SUN_1432, 630, &t);
    clock_format_time(&t, true, time);
    clock_format_date(&t, date);
    TEST_ASSERT_EQUAL_STRING("01:02", time);
    TEST_ASSERT_EQUAL_STRING("MON 16 NOV", date);
    TEST_ASSERT_TRUE(clock_is_night(&t));

    /* -12:00 stays on Sunday morning. */
    clock_local(T_SUN_1432, -720, &t);
    clock_format_time(&t, false, time);
    TEST_ASSERT_EQUAL_STRING("2:32", time);
    TEST_ASSERT_TRUE(clock_is_night(&t));

    /* Midnight and noon in 12 h. */
    clock_local(1794700800000LL, 0, &t);
    clock_format_time(&t, false, time);
    TEST_ASSERT_EQUAL_STRING("12:00", time);
    clock_format_time(&t, true, time);
    TEST_ASSERT_EQUAL_STRING("00:00", time);
    clock_local(1794700800000LL + 12 * 3600000LL, 0, &t);
    clock_format_time(&t, false, time);
    TEST_ASSERT_EQUAL_STRING("12:00", time);
}

static void test_leap_and_year_end(void)
{
    clock_tm_t t;
    char date[CLOCK_DATE_SIZE];
    /* 2028-02-29T12:00Z, a Tuesday. */
    clock_local(1835438400000LL, 0, &t);
    clock_format_date(&t, date);
    TEST_ASSERT_EQUAL_STRING("TUE 29 FEB", date);
    TEST_ASSERT_EQUAL(2028, t.year);
    /* 2026-12-31T23:59:59Z plus one hour is 2027-01-01. */
    clock_local(1798761599000LL, 60, &t);
    clock_format_date(&t, date);
    TEST_ASSERT_EQUAL_STRING("FRI 1 JAN", date);
    TEST_ASSERT_EQUAL(2027, t.year);
}

static void test_night_edges(void)
{
    const struct {
        uint8_t hour;
        bool night;
    } cases[] = {{21, false}, {22, true}, {23, true}, {0, true}, {6, true}, {7, false}, {12, false}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const clock_tm_t t = {.hour = cases[i].hour, .minute = 59};
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].night, clock_is_night(&t), "hour");
    }
}

static void test_ms_to_minute(void)
{
    TEST_ASSERT_EQUAL_UINT32(50000, clock_ms_to_minute(T_SUN_1432));
    TEST_ASSERT_EQUAL_UINT32(60000, clock_ms_to_minute(T_SUN_1432 + 50000));
    TEST_ASSERT_EQUAL_UINT32(1, clock_ms_to_minute(T_SUN_1432 + 49999));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid);
    RUN_TEST(test_local_and_format);
    RUN_TEST(test_leap_and_year_end);
    RUN_TEST(test_night_edges);
    RUN_TEST(test_ms_to_minute);
    return UNITY_END();
}
