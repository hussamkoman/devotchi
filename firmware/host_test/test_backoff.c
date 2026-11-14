#include "backoff.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t fixed_rand(void *ctx)
{
    return *(const uint32_t *)ctx;
}

/* xorshift32, deterministic. */
static uint32_t prng(void *ctx)
{
    uint32_t *s = ctx;
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void test_doubling_without_jitter(void)
{
    backoff_t b;
    backoff_init(&b, 1000, 30000, NULL, NULL);
    static const uint32_t exp[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000};
    for (size_t i = 0; i < sizeof(exp) / sizeof(exp[0]); i++) {
        TEST_ASSERT_EQUAL_UINT32(exp[i], backoff_next(&b));
    }
    backoff_reset(&b);
    TEST_ASSERT_EQUAL_UINT32(1000, backoff_next(&b));
    TEST_ASSERT_EQUAL_UINT32(2000, backoff_next(&b));
}

static void test_websocket_sequence(void)
{
    backoff_t b;
    backoff_init(&b, 1000, 60000, NULL, NULL);
    static const uint32_t exp[] = {1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000};
    for (size_t i = 0; i < sizeof(exp) / sizeof(exp[0]); i++) {
        TEST_ASSERT_EQUAL_UINT32(exp[i], backoff_next(&b));
    }
}

static void test_jitter_extremes(void)
{
    backoff_t b;
    uint32_t r = 0;
    backoff_init(&b, 1000, 30000, fixed_rand, &r);
    TEST_ASSERT_EQUAL_UINT32(800, backoff_next(&b));

    r = 2 * BACKOFF_JITTER_PERMILLE;
    backoff_init(&b, 1000, 30000, fixed_rand, &r);
    TEST_ASSERT_EQUAL_UINT32(1200, backoff_next(&b));
    TEST_ASSERT_EQUAL_UINT32(2400, backoff_next(&b));

    r = BACKOFF_JITTER_PERMILLE;
    backoff_init(&b, 1000, 30000, fixed_rand, &r);
    TEST_ASSERT_EQUAL_UINT32(1000, backoff_next(&b));

    r = UINT32_MAX;
    backoff_init(&b, 1000, 30000, fixed_rand, &r);
    const uint32_t d = backoff_next(&b);
    TEST_ASSERT_TRUE(d >= 800 && d <= 1200);
}

static void test_jitter_bounds_and_spread(void)
{
    backoff_t b;
    uint32_t seed = 12345;
    backoff_init(&b, 1000, 60000, prng, &seed);
    uint32_t lo = UINT32_MAX;
    uint32_t hi = 0;
    for (int round = 0; round < 500; round++) {
        backoff_reset(&b);
        uint32_t base = 1000;
        for (int i = 0; i < 10; i++) {
            const uint32_t d = backoff_next(&b);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT32(base * 8 / 10, d);
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(base * 12 / 10, d);
            if (i == 0) {
                lo = d < lo ? d : lo;
                hi = d > hi ? d : hi;
            }
            base = base * 2 > 60000 ? 60000 : base * 2;
        }
    }
    /* The jitter actually spreads. */
    TEST_ASSERT_LESS_THAN_UINT32(850, lo);
    TEST_ASSERT_GREATER_THAN_UINT32(1150, hi);
}

static void test_degenerate_limits(void)
{
    backoff_t b;
    backoff_init(&b, 0, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, backoff_next(&b));
    TEST_ASSERT_EQUAL_UINT32(1, backoff_next(&b));

    backoff_init(&b, 5000, 1000, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(5000, backoff_next(&b));
    TEST_ASSERT_EQUAL_UINT32(5000, backoff_next(&b));

    /* No overflow near the top of the range. */
    backoff_init(&b, 3000000000U, UINT32_MAX, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(3000000000U, backoff_next(&b));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, backoff_next(&b));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, backoff_next(&b));

    uint32_t r = 2 * BACKOFF_JITTER_PERMILLE;
    backoff_init(&b, UINT32_MAX, UINT32_MAX, fixed_rand, &r);
    /* +20 % of the maximum does not fit 32 bits: saturates instead of wrapping. */
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, backoff_next(&b));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_doubling_without_jitter);
    RUN_TEST(test_websocket_sequence);
    RUN_TEST(test_jitter_extremes);
    RUN_TEST(test_jitter_bounds_and_spread);
    RUN_TEST(test_degenerate_limits);
    return UNITY_END();
}
