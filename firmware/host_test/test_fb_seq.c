#include "fb_seq.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    uint32_t t;
    uint8_t led;
    uint8_t motor;
} timed_t;

#define MAX_STEPS 64

typedef struct {
    timed_t steps[MAX_STEPS];
    size_t count;
    uint32_t end_ms;
    bool ended;
} trace_t;

/* Polls exactly at every deadline, as the driver's one-shot timer does, until done or until limit_ms. */
static void run(fb_seq_t *s, uint32_t start, uint32_t limit_ms, trace_t *tr)
{
    memset(tr, 0, sizeof(*tr));
    uint32_t now = start;
    for (;;) {
        fb_seq_out_t out;
        fb_seq_poll(s, now, &out);
        if (out.done) {
            tr->ended = true;
            tr->end_ms = now - start;
            return;
        }
        if (out.step_started && tr->count < MAX_STEPS) {
            tr->steps[tr->count++] = (timed_t){now - start, out.led_pct, out.motor_pct};
        }
        TEST_ASSERT_TRUE(out.next_deadline_ms != now);
        now = out.next_deadline_ms;
        if (now - start > limit_ms) {
            return;
        }
    }
}

static void expect_trace(const char *name, const trace_t *tr, const timed_t *exp, size_t n)
{
    char msg[64];
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(n, tr->count);
    for (size_t i = 0; i < n; i++) {
        snprintf(msg, sizeof(msg), "%s step %zu", name, i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(exp[i].t, tr->steps[i].t, msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(exp[i].led, tr->steps[i].led, msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(exp[i].motor, tr->steps[i].motor, msg);
    }
}

static void play_once(fb_seq_id_t id, const timed_t *exp, size_t n, uint32_t total_ms)
{
    fb_seq_t s;
    trace_t tr;
    fb_seq_init(&s, 100);
    TEST_ASSERT_TRUE(fb_seq_start(&s, id, 0, 1000));
    run(&s, 1000, 60000, &tr);
    const char *name = fb_seq_pattern(id)->name;
    TEST_ASSERT_TRUE_MESSAGE(tr.ended, name);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(n, tr.count, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(total_ms, tr.end_ms, name);
    expect_trace(name, &tr, exp, n);
    TEST_ASSERT_FALSE(fb_seq_running(&s));
}

static void test_table_is_sane(void)
{
    for (int i = 0; i < FB_SEQ_COUNT; i++) {
        const fb_seq_pattern_t *p = fb_seq_pattern((fb_seq_id_t)i);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_GREATER_THAN_UINT8(0, p->count);
        TEST_ASSERT_LESS_THAN_UINT8(p->count, p->loop_from);
        for (int j = 0; j < p->count; j++) {
            TEST_ASSERT_GREATER_THAN_UINT16(0, p->steps[j].ms);
            TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, p->steps[j].led_pct);
            TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, p->steps[j].motor_pct);
        }
        fb_seq_id_t found;
        TEST_ASSERT_TRUE(fb_seq_find(p->name, &found));
        TEST_ASSERT_EQUAL(i, found);
    }
    fb_seq_id_t found;
    TEST_ASSERT_FALSE(fb_seq_find("buzz", &found));
    TEST_ASSERT_NULL(fb_seq_pattern(FB_SEQ_COUNT));
}

static void test_priorities(void)
{
    static const struct {
        fb_seq_id_t id;
        uint8_t prio;
        bool loop;
    } exp[] = {
        {FB_SEQ_IDENTIFY, 3, true}, {FB_SEQ_REQUEST, 2, true}, {FB_SEQ_ATTENTION, 2, true},
        {FB_SEQ_NAG, 2, false},     {FB_SEQ_NEGATIVE, 1, false}, {FB_SEQ_COMPLETE, 1, false},
        {FB_SEQ_POSITIVE, 1, false}, {FB_SEQ_TICK, 0, false},   {FB_SEQ_CLICK, 0, false},
    };
    for (size_t i = 0; i < sizeof(exp) / sizeof(exp[0]); i++) {
        TEST_ASSERT_EQUAL_UINT8(exp[i].prio, fb_seq_pattern(exp[i].id)->priority);
        TEST_ASSERT_EQUAL(exp[i].loop, fb_seq_pattern(exp[i].id)->loop);
    }
}

static void test_nag_tick_click(void)
{
    static const timed_t nag[] = {{0, 0, 100}};
    play_once(FB_SEQ_NAG, nag, 1, 60);
    static const timed_t tick[] = {{0, 0, 50}};
    play_once(FB_SEQ_TICK, tick, 1, 30);
    static const timed_t click[] = {{0, 100, 0}};
    play_once(FB_SEQ_CLICK, click, 1, 40);
}

static void test_negative(void)
{
    static const timed_t exp[] = {
        {0, 100, 60}, {250, 100, 0}, {300, 0, 0}, {400, 0, 60},
        {600, 100, 60}, {650, 100, 0}, {900, 0, 0},
    };
    play_once(FB_SEQ_NEGATIVE, exp, sizeof(exp) / sizeof(exp[0]), 1200);
}

static void test_complete(void)
{
    static const timed_t exp[] = {
        {0, 100, 50}, {60, 100, 0}, {120, 100, 100}, {200, 75, 100},
        {270, 75, 0}, {400, 50, 0}, {600, 25, 0}, {800, 0, 0},
    };
    play_once(FB_SEQ_COMPLETE, exp, sizeof(exp) / sizeof(exp[0]), 1000);
}

static void test_positive(void)
{
    static const timed_t exp[] = {
        {0, 100, 60}, {40, 100, 0}, {80, 0, 0}, {160, 100, 0},
        {240, 0, 0}, {320, 100, 0}, {400, 0, 0},
    };
    play_once(FB_SEQ_POSITIVE, exp, sizeof(exp) / sizeof(exp[0]), 480);
}

static void test_request_then_breathing_loop(void)
{
    fb_seq_t s;
    trace_t tr;
    fb_seq_init(&s, 100);
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_REQUEST, 0, 0));
    run(&s, 0, 4320, &tr);
    TEST_ASSERT_FALSE(tr.ended);
    static const timed_t exp[] = {
        {0, 100, 100}, {120, 100, 0}, {200, 100, 100},
        {320, 0, 0}, {570, 25, 0}, {820, 50, 0}, {1070, 75, 0},
        {1320, 100, 0}, {1570, 75, 0}, {1820, 50, 0}, {2070, 25, 0},
        {2320, 0, 0}, {2570, 25, 0}, {2820, 50, 0},
    };
    expect_trace("request", &tr, exp, sizeof(exp) / sizeof(exp[0]));
    TEST_ASSERT_TRUE(fb_seq_running(&s));
}

static void test_attention_loop(void)
{
    fb_seq_t s;
    trace_t tr;
    fb_seq_init(&s, 100);
    fb_seq_start(&s, FB_SEQ_ATTENTION, 0, 0);
    run(&s, 0, 6000, &tr);
    static const timed_t exp[] = {
        {0, 0, 100}, {100, 0, 0}, {200, 0, 100},
        {300, 100, 0}, {500, 0, 0}, {2300, 100, 0}, {2500, 0, 0}, {4300, 100, 0},
    };
    expect_trace("attention", &tr, exp, sizeof(exp) / sizeof(exp[0]));
}

static void test_identify_for_duration(void)
{
    fb_seq_t s;
    trace_t tr;
    fb_seq_init(&s, 100);
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_IDENTIFY, 2000, 500));
    run(&s, 500, 10000, &tr);
    TEST_ASSERT_TRUE(tr.ended);
    TEST_ASSERT_EQUAL_UINT32(2000, tr.end_ms);
    static const timed_t exp[] = {
        {0, 100, 80}, {100, 100, 0}, {300, 100, 80}, {400, 100, 0}, {600, 100, 80}, {700, 100, 0},
        {900, 100, 80}, {1000, 100, 0}, {1200, 100, 80}, {1300, 100, 0}, {1500, 100, 80}, {1600, 100, 0},
        {1800, 100, 80}, {1900, 100, 0},
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(exp) / sizeof(exp[0]), tr.count);
    expect_trace("identify", &tr, exp, sizeof(exp) / sizeof(exp[0]));
}

static void test_duration_cuts_one_shot(void)
{
    fb_seq_t s;
    trace_t tr;
    fb_seq_init(&s, 100);
    fb_seq_start(&s, FB_SEQ_COMPLETE, 100, 0);
    run(&s, 0, 10000, &tr);
    TEST_ASSERT_TRUE(tr.ended);
    TEST_ASSERT_EQUAL_UINT32(100, tr.end_ms);
    TEST_ASSERT_EQUAL_size_t(2, tr.count);
}

static void test_preemption(void)
{
    fb_seq_t s;
    fb_seq_init(&s, 100);
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_REQUEST, 0, 0));
    TEST_ASSERT_FALSE(fb_seq_start(&s, FB_SEQ_CLICK, 0, 10));
    TEST_ASSERT_FALSE(fb_seq_start(&s, FB_SEQ_COMPLETE, 0, 10));
    TEST_ASSERT_EQUAL(FB_SEQ_REQUEST, fb_seq_current(&s));
    /* Equal priority replaces. */
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_NAG, 0, 20));
    TEST_ASSERT_EQUAL(FB_SEQ_NAG, fb_seq_current(&s));
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_IDENTIFY, 0, 30));
    TEST_ASSERT_FALSE(fb_seq_start(&s, FB_SEQ_ATTENTION, 0, 40));

    fb_seq_out_t out;
    fb_seq_poll(&s, 40, &out);
    TEST_ASSERT_TRUE(out.step_started);
    TEST_ASSERT_EQUAL_UINT8(0, out.step);
    TEST_ASSERT_EQUAL_UINT32(130, out.next_deadline_ms);

    /* A finished pattern no longer blocks a lower priority. */
    fb_seq_stop(&s);
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_NAG, 0, 0));
    fb_seq_poll(&s, 60, &out);
    TEST_ASSERT_TRUE(out.done);
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_TICK, 0, 70));
    TEST_ASSERT_FALSE(fb_seq_start(&s, FB_SEQ_COUNT, 0, 70));
}

static void test_loop_stop(void)
{
    fb_seq_t s;
    fb_seq_out_t out;
    fb_seq_init(&s, 100);
    fb_seq_start(&s, FB_SEQ_ATTENTION, 0, 0);
    fb_seq_poll(&s, 100000, &out);
    TEST_ASSERT_FALSE(out.done);
    fb_seq_stop(&s);
    TEST_ASSERT_FALSE(fb_seq_running(&s));
    TEST_ASSERT_EQUAL(FB_SEQ_COUNT, fb_seq_current(&s));
    fb_seq_poll(&s, 100001, &out);
    TEST_ASSERT_TRUE(out.done);
    TEST_ASSERT_EQUAL_UINT8(0, out.led_pct);
    TEST_ASSERT_EQUAL_UINT8(0, out.motor_pct);
    /* Lowest priority starts after a stop. */
    TEST_ASSERT_TRUE(fb_seq_start(&s, FB_SEQ_CLICK, 0, 100002));
}

static void test_late_poll_keeps_schedule(void)
{
    fb_seq_t s;
    fb_seq_out_t out;
    fb_seq_init(&s, 100);
    fb_seq_start(&s, FB_SEQ_POSITIVE, 0, 0);
    fb_seq_poll(&s, 0, &out);
    /* 15 ms late for the 40 ms boundary: the next boundary stays at 80. */
    fb_seq_poll(&s, 55, &out);
    TEST_ASSERT_EQUAL_UINT8(1, out.step);
    TEST_ASSERT_TRUE(out.step_started);
    TEST_ASSERT_EQUAL_UINT32(80, out.next_deadline_ms);
    /* A repeated poll in the same step reports no new step. */
    fb_seq_poll(&s, 60, &out);
    TEST_ASSERT_FALSE(out.step_started);
    /* Skipping over whole steps lands on the right one. */
    fb_seq_poll(&s, 250, &out);
    TEST_ASSERT_EQUAL_UINT8(4, out.step);
    TEST_ASSERT_EQUAL_UINT32(320, out.next_deadline_ms);
}

static void test_scaling(void)
{
    static const struct {
        fb_seq_haptic_t haptic;
        bool led;
        uint8_t max;
        uint8_t motor;
        uint8_t led_out;
    } cases[] = {
        {FB_SEQ_HAPTIC_FULL, true, 100, 100, 100},
        {FB_SEQ_HAPTIC_FULL, true, 70, 70, 100},
        {FB_SEQ_HAPTIC_GENTLE, true, 70, 35, 100},
        {FB_SEQ_HAPTIC_GENTLE, false, 100, 50, 0},
        {FB_SEQ_HAPTIC_OFF, true, 70, 0, 100},
        {FB_SEQ_HAPTIC_OFF, false, 70, 0, 0},
        {FB_SEQ_HAPTIC_FULL, true, 200, 100, 100},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fb_seq_t s;
        fb_seq_out_t out;
        fb_seq_init(&s, cases[i].max);
        fb_seq_set_output(&s, cases[i].haptic, cases[i].led);
        fb_seq_start(&s, FB_SEQ_REQUEST, 0, 0);
        fb_seq_poll(&s, 0, &out);
        TEST_ASSERT_EQUAL_UINT8(cases[i].motor, out.motor_pct);
        TEST_ASSERT_EQUAL_UINT8(cases[i].led_out, out.led_pct);
    }

    /* 60 % of a 70 % maximum at gentle: 21 %. */
    fb_seq_t s;
    fb_seq_out_t out;
    fb_seq_init(&s, 70);
    fb_seq_set_output(&s, FB_SEQ_HAPTIC_GENTLE, true);
    fb_seq_start(&s, FB_SEQ_NEGATIVE, 0, 0);
    fb_seq_poll(&s, 0, &out);
    TEST_ASSERT_EQUAL_UINT8(21, out.motor_pct);
    /* A setting change applies to the running pattern at the next poll. */
    fb_seq_set_output(&s, FB_SEQ_HAPTIC_OFF, false);
    fb_seq_poll(&s, 10, &out);
    TEST_ASSERT_EQUAL_UINT8(0, out.motor_pct);
    TEST_ASSERT_EQUAL_UINT8(0, out.led_pct);
}

static void test_wraparound(void)
{
    fb_seq_t s;
    trace_t tr;
    fb_seq_init(&s, 100);
    const uint32_t start = UINT32_MAX - 500;
    fb_seq_start(&s, FB_SEQ_IDENTIFY, 2000, start);
    run(&s, start, 10000, &tr);
    TEST_ASSERT_TRUE(tr.ended);
    TEST_ASSERT_EQUAL_UINT32(2000, tr.end_ms);
    TEST_ASSERT_EQUAL_size_t(14, tr.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_is_sane);
    RUN_TEST(test_priorities);
    RUN_TEST(test_nag_tick_click);
    RUN_TEST(test_negative);
    RUN_TEST(test_complete);
    RUN_TEST(test_positive);
    RUN_TEST(test_request_then_breathing_loop);
    RUN_TEST(test_attention_loop);
    RUN_TEST(test_identify_for_duration);
    RUN_TEST(test_duration_cuts_one_shot);
    RUN_TEST(test_preemption);
    RUN_TEST(test_loop_stop);
    RUN_TEST(test_late_poll_keeps_schedule);
    RUN_TEST(test_scaling);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
