#include "pet_pose.h"

#include "unity.h"

#include <stdbool.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

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

/* pet_pose_t is an array of int16_t channels without padding. */
static bool same_pose(const pet_pose_t *a, const pet_pose_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static uint32_t fixed_rand(void *ctx)
{
    return *(const uint32_t *)ctx;
}

static void test_every_mood_has_a_distinct_pose(void)
{
    for (int m = 0; m < MOOD_COUNT; m++) {
        const pet_pose_t *p = pet_pose_for_mood((proto_mood_t)m);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_TRUE_MESSAGE(p->eye_size > 0, proto_mood_name((proto_mood_t)m));
        TEST_ASSERT_TRUE(p->eye_open >= 0 && p->eye_open <= 100);
        TEST_ASSERT_TRUE(p->pupil_size > 0 && p->pupil_size < 100);
        TEST_ASSERT_TRUE(p->mouth_curve >= -100 && p->mouth_curve <= 100);
        TEST_ASSERT_TRUE(p->pupil_dx >= -100 && p->pupil_dx <= 100 && p->pupil_dy >= -100 && p->pupil_dy <= 100);
        TEST_ASSERT_TRUE(p->bob_period_ms > 0);
        TEST_ASSERT_TRUE(p->accessory < PET_ACC_COUNT);
        for (int o = 0; o < m; o++) {
            TEST_ASSERT_FALSE_MESSAGE(same_pose(p, pet_pose_for_mood((proto_mood_t)o)),
                                     "two moods share a pose");
        }
    }
    TEST_ASSERT_EQUAL_PTR(pet_pose_for_mood(MOOD_IDLE), pet_pose_for_mood(MOOD_COUNT));
    TEST_ASSERT_EQUAL_PTR(pet_pose_for_mood(MOOD_IDLE), pet_pose_for_mood((proto_mood_t)-1));
}

static void test_moods_match_their_description(void)
{
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_HAPPY)->mouth_curve > 0);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_SAD)->mouth_curve < 0);
    TEST_ASSERT_EQUAL(PET_ACC_TEAR, pet_pose_for_mood(MOOD_SAD)->accessory);
    TEST_ASSERT_EQUAL(PET_ACC_SPARKLES, pet_pose_for_mood(MOOD_EXCITED)->accessory);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_EXCITED)->bob_period_ms < pet_pose_for_mood(MOOD_IDLE)->bob_period_ms);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_EXCITED)->eye_size > pet_pose_for_mood(MOOD_IDLE)->eye_size);
    TEST_ASSERT_EQUAL(1, pet_pose_for_mood(MOOD_ANXIOUS)->jitter_px);
    TEST_ASSERT_EQUAL(PET_ACC_SWEAT, pet_pose_for_mood(MOOD_ANXIOUS)->accessory);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_ANXIOUS)->pupil_size < pet_pose_for_mood(MOOD_IDLE)->pupil_size);
    TEST_ASSERT_EQUAL(0, pet_pose_for_mood(MOOD_SLEEPING)->eye_open);
    TEST_ASSERT_EQUAL(0, pet_pose_for_mood(MOOD_SLEEPING)->blink_min_ms);
    TEST_ASSERT_EQUAL(PET_ACC_ZZZ, pet_pose_for_mood(MOOD_SLEEPING)->accessory);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_SLEEPING)->bob_period_ms > pet_pose_for_mood(MOOD_IDLE)->bob_period_ms);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_ANGRY)->brow_angle > 0);
    TEST_ASSERT_EQUAL(PET_ACC_SPARKLES, pet_pose_for_mood(MOOD_PROUD)->accessory);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_PROUD)->pupil_dy < 0);
    TEST_ASSERT_EQUAL(PET_GLANCE_OFF, pet_pose_for_mood(MOOD_FOCUSED)->glance);
    TEST_ASSERT_EQUAL(0, pet_pose_for_mood(MOOD_FOCUSED)->lid);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_FOCUSED)->eye_open < 70);
    TEST_ASSERT_EQUAL(PET_ACC_TONGUE, pet_pose_for_mood(MOOD_WORKING)->accessory);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_IDLE)->mouth_curve >= 40);
    TEST_ASSERT_EQUAL(PET_PACK_LID_OFF, pet_pose_for_mood(MOOD_IDLE)->lid);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_THINKING)->brow_lift > 0 && pet_pose_for_mood(MOOD_THINKING)->mouth_dx > 0);
    TEST_ASSERT_EQUAL(PET_GLANCE_SCAN, pet_pose_for_mood(MOOD_WORKING)->glance);
    TEST_ASSERT_TRUE(pet_pose_for_mood(MOOD_THINKING)->pupil_dx > 0 && pet_pose_for_mood(MOOD_THINKING)->pupil_dy < 0);
    TEST_ASSERT_EQUAL(PET_ACC_DOTS, pet_pose_for_mood(MOOD_THINKING)->accessory);
    TEST_ASSERT_EQUAL_STRING("dots", pet_accessory_name(PET_ACC_DOTS));
    TEST_ASSERT_EQUAL_STRING("none", pet_accessory_name(PET_ACC_COUNT));
}

static void test_ease_out(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, pet_ease_out(0));
    TEST_ASSERT_EQUAL_UINT16(750, pet_ease_out(500));
    TEST_ASSERT_EQUAL_UINT16(1000, pet_ease_out(1000));
    TEST_ASSERT_EQUAL_UINT16(1000, pet_ease_out(65535));
    uint16_t last = 0;
    for (uint16_t t = 0; t <= 1000; t += 10) {
        const uint16_t e = pet_ease_out(t);
        TEST_ASSERT_TRUE(e >= last);
        TEST_ASSERT_TRUE(e >= t);
        last = e;
    }
}

static void test_lerp_endpoints_and_midpoint(void)
{
    const pet_pose_t *a = pet_pose_for_mood(MOOD_HAPPY);
    const pet_pose_t *b = pet_pose_for_mood(MOOD_SAD);
    pet_pose_t out;
    pet_pose_lerp(a, b, 0, &out);
    TEST_ASSERT_TRUE(same_pose(a, &out));
    pet_pose_lerp(a, b, 1000, &out);
    TEST_ASSERT_TRUE(same_pose(b, &out));
    pet_pose_lerp(a, b, 2000, &out);
    TEST_ASSERT_TRUE(same_pose(b, &out));

    const pet_pose_t x = {.eye_open = 0, .mouth_curve = -100, .bob_period_ms = 1000, .accessory = PET_ACC_NONE};
    const pet_pose_t y = {.eye_open = 100, .mouth_curve = 100, .bob_period_ms = 3000, .accessory = PET_ACC_ZZZ};
    /* Ease-out: half the time covers three quarters of the way. */
    pet_pose_lerp(&x, &y, 500, &out);
    TEST_ASSERT_EQUAL_INT16(75, out.eye_open);
    TEST_ASSERT_EQUAL_INT16(50, out.mouth_curve);
    TEST_ASSERT_EQUAL_INT16(2500, out.bob_period_ms);
    TEST_ASSERT_EQUAL_UINT8(PET_ACC_ZZZ, out.accessory);
    pet_pose_lerp(&x, &y, 499, &out);
    TEST_ASSERT_EQUAL_UINT8(PET_ACC_NONE, out.accessory);
}

static void test_lerp_colour_per_channel(void)
{
    /* 75 % of the way per channel, no carry between channels. */
    TEST_ASSERT_EQUAL_HEX32(0xBF4060, pet_color_lerp(0x00FF00, 0xFF0080, 500));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, pet_color_lerp(0x00FF00, 0xFF0080, 0));
    TEST_ASSERT_EQUAL_HEX32(0xFF0080, pet_color_lerp(0x00FF00, 0xFF0080, 1000));
}

static void test_lid_switches_halfway_when_it_appears(void)
{
    pet_pose_t a = *pet_pose_for_mood(MOOD_IDLE);
    pet_pose_t b = *pet_pose_for_mood(MOOD_FOCUSED);
    pet_pose_t out;
    pet_pose_lerp(&a, &b, 499, &out);
    TEST_ASSERT_EQUAL(PET_PACK_LID_OFF, out.lid);
    pet_pose_lerp(&a, &b, 500, &out);
    TEST_ASSERT_EQUAL(0, out.lid);
    a.lid = -20;
    b.lid = 20;
    pet_pose_lerp(&a, &b, 500, &out);
    TEST_ASSERT_EQUAL(10, out.lid);
}

static void test_channel_table_follows_the_format(void)
{
    pet_pose_t p = {0};
    *pet_pose_channel(&p, PET_PACK_CH_EYE_OPEN) = 1;
    *pet_pose_channel(&p, PET_PACK_CH_ACCESSORY) = 2;
    *pet_pose_channel(&p, PET_PACK_CH_GLANCE) = 3;
    *pet_pose_channel(&p, PET_PACK_CH_LID) = 4;
    *pet_pose_channel(&p, PET_PACK_CH_BROW_LIFT) = 5;
    *pet_pose_channel(&p, PET_PACK_CH_MOUTH_W) = 6;
    TEST_ASSERT_EQUAL(1, p.eye_open);
    TEST_ASSERT_EQUAL(2, p.accessory);
    TEST_ASSERT_EQUAL(3, p.glance);
    TEST_ASSERT_EQUAL(4, p.lid);
    TEST_ASSERT_EQUAL(5, p.brow_lift);
    TEST_ASSERT_EQUAL(6, p.mouth_w);
    TEST_ASSERT_NULL(pet_pose_channel(&p, PET_PACK_CH_COUNT));
}

static void test_overrides_apply_per_mood(void)
{
    pet_def_t def = {0};
    def.n_over = 2;
    def.over[0] = (pet_override_t){.mood = PET_PACK_MOOD_IDLE, .channel = PET_PACK_CH_MOUTH_CURVE, .value = 30};
    def.over[1] = (pet_override_t){.mood = PET_PACK_REACT_POKE, .channel = PET_PACK_CH_LID, .value = -6};
    pet_pose_t p;
    pet_pose_for_pet(&def, MOOD_IDLE, &p);
    TEST_ASSERT_EQUAL(30, p.mouth_curve);
    pet_pose_for_pet(&def, MOOD_HAPPY, &p);
    TEST_ASSERT_EQUAL(80, p.mouth_curve);
    pet_pose_for_pet(NULL, MOOD_IDLE, &p);
    TEST_ASSERT_EQUAL(42, p.mouth_curve);
    pet_pose_t r;
    pet_pose_react(&def, PET_REACT_POKE, &p, &r);
    TEST_ASSERT_EQUAL(25, r.eye_open);
    TEST_ASSERT_EQUAL(-6, r.lid);
    pet_pose_react(NULL, PET_REACT_POKE, &p, &r);
    TEST_ASSERT_EQUAL(PET_PACK_LID_OFF, r.lid);
    pet_pose_react(&def, PET_REACT_COMPLETE, &p, &r);
    TEST_ASSERT_TRUE(same_pose(pet_pose_for_mood(MOOD_PROUD), &r));
    pet_pose_react(&def, PET_REACT_POSITIVE, &p, &r);
    TEST_ASSERT_TRUE(same_pose(&p, &r));
    /* The reaction leaves the channels it does not name alone. */
    pet_pose_react(NULL, PET_REACT_PAT, &p, &r);
    TEST_ASSERT_EQUAL(p.eye_open, r.eye_open);
    TEST_ASSERT_EQUAL(90, r.mouth_curve);
}

static void test_reaction_timelines(void)
{
    pet_react_frame_t f;
    for (int r = PET_REACT_PAT; r < PET_REACT_COUNT; r++) {
        pet_react_sample((pet_react_t)r, 0, &f);
        TEST_ASSERT_TRUE_MESSAGE(f.active, pet_react_name((pet_react_t)r));
        TEST_ASSERT_EQUAL((pet_react_t)r, pet_react_parse(pet_react_name((pet_react_t)r)));
        if (pet_react_ends((pet_react_t)r)) {
            pet_react_sample((pet_react_t)r, pet_react_ms((pet_react_t)r), &f);
            TEST_ASSERT_FALSE(f.active);
            TEST_ASSERT_EQUAL(1000, f.lids);
        }
    }
    TEST_ASSERT_EQUAL(PET_REACT_COUNT, pet_react_parse("hug"));
    pet_react_sample(PET_REACT_NONE, 0, &f);
    TEST_ASSERT_FALSE(f.active);

    pet_react_sample(PET_REACT_PAT, 600, &f);
    TEST_ASSERT_EQUAL(1000, f.weight);
    TEST_ASSERT_EQUAL(PET_EYES_ARC, f.eyes);
    TEST_ASSERT_EQUAL(PET_FX_HEARTS, f.fx);
    TEST_ASSERT_EQUAL(400, f.fx_permille);

    pet_react_sample(PET_REACT_POKE, 80, &f);
    TEST_ASSERT_EQUAL(12, f.squish);
    TEST_ASSERT_EQUAL(PET_FX_IMPACT, f.fx);
    pet_react_sample(PET_REACT_POKE, 400, &f);
    TEST_ASSERT_EQUAL(0, f.squish);
    TEST_ASSERT_EQUAL(PET_FX_NONE, f.fx);

    pet_react_sample(PET_REACT_DIZZY, 1000, &f);
    TEST_ASSERT_EQUAL(PET_EYES_SPIRAL, f.eyes);
    TEST_ASSERT_TRUE(f.sway >= -2 && f.sway <= 2);

    /* Wake: eyes open first, then the yawn stretches the body, then a blink. */
    pet_react_sample(PET_REACT_WAKE, 100, &f);
    TEST_ASSERT_EQUAL(PET_FX_ZZZ1, f.fx);
    TEST_ASSERT_EQUAL(0, f.mouth);
    pet_react_sample(PET_REACT_WAKE, 700, &f);
    TEST_ASSERT_EQUAL(1000, f.mouth);
    TEST_ASSERT_EQUAL(-8, f.squish);
    TEST_ASSERT_EQUAL(1000, f.weight);
    pet_react_sample(PET_REACT_WAKE, 900, &f);
    TEST_ASSERT_EQUAL(0, f.lids);
    pet_react_sample(PET_REACT_WAKE, 1200, &f);
    TEST_ASSERT_TRUE(f.weight < 1000 && f.weight > 0);

    /* Attention: hop at the start of every 4 s cycle, the badge stays once it popped in. */
    pet_react_sample(PET_REACT_ATTENTION, 150, &f);
    TEST_ASSERT_EQUAL(6, f.hop);
    TEST_ASSERT_EQUAL(PET_FX_NONE, f.fx);
    pet_react_sample(PET_REACT_ATTENTION, 1000, &f);
    TEST_ASSERT_EQUAL(0, f.hop);
    TEST_ASSERT_EQUAL(PET_FX_BANG, f.fx);
    pet_react_sample(PET_REACT_ATTENTION, 4150, &f);
    TEST_ASSERT_EQUAL(6, f.hop);
    TEST_ASSERT_EQUAL(PET_FX_BANG, f.fx);
    TEST_ASSERT_EQUAL(1000, f.weight);
    pet_react_sample(PET_REACT_ATTENTION, 4000 * 50 + 10, &f);
    TEST_ASSERT_TRUE(f.active);

    pet_react_sample(PET_REACT_POSITIVE, 150, &f);
    TEST_ASSERT_EQUAL(5, f.hop);
    TEST_ASSERT_EQUAL(PET_FX_SPARKLES, f.fx);
    pet_react_sample(PET_REACT_NEGATIVE, 200, &f);
    TEST_ASSERT_EQUAL(-3, f.hop);
    pet_react_sample(PET_REACT_COMPLETE, 1000, &f);
    TEST_ASSERT_EQUAL(1000, f.weight);
    TEST_ASSERT_EQUAL(MOOD_PROUD, pet_react_aux_row(PET_REACT_COMPLETE));
    TEST_ASSERT_EQUAL(PET_PACK_MOODS + 4, pet_react_aux_row(PET_REACT_ATTENTION));
    TEST_ASSERT_EQUAL(-1, pet_react_aux_row(PET_REACT_POSITIVE));
}

static void test_sin(void)
{
    TEST_ASSERT_EQUAL(0, pet_sin(0, 100));
    TEST_ASSERT_EQUAL(100, pet_sin(90, 100));
    TEST_ASSERT_EQUAL(-100, pet_sin(270, 100));
    TEST_ASSERT_EQUAL(-100, pet_sin(-90, 100));
    TEST_ASSERT_INT_WITHIN(1, 50, pet_sin(30, 100));
    TEST_ASSERT_EQUAL(0, pet_sin(180, 100));
    TEST_ASSERT_EQUAL(100, pet_sin(450, 100));
}

static void test_aux_lerp_and_oscillators(void)
{
    const int16_t a[4] = {0, 0, 0, 0};
    const int16_t b[4] = {20, -8, 3, 1};
    int16_t cur[4], sel[4];
    pet_aux_lerp(a, b, 500, cur, sel);
    TEST_ASSERT_EQUAL(15, cur[0]);
    TEST_ASSERT_EQUAL(-6, cur[1]);
    TEST_ASSERT_EQUAL(3, sel[2]);
    pet_aux_lerp(a, b, 499, cur, sel);
    TEST_ASSERT_EQUAL(0, sel[2]);

    const pet_osc_t sine = {.wave = PET_WAVE_SINE, .amp = 3, .period_ms = 4000};
    TEST_ASSERT_EQUAL(0, pet_osc_value(&sine, 0, 0, 0, 0));
    TEST_ASSERT_EQUAL(3, pet_osc_value(&sine, 1000, 0, 0, 0));
    TEST_ASSERT_EQUAL(-3, pet_osc_value(&sine, 7000, 0, 0, 0));
    const pet_osc_t bob = {.wave = PET_WAVE_BOB, .amp = 10};
    TEST_ASSERT_EQUAL(-5, pet_osc_value(&bob, 0, -500, 0, 0));
    const pet_osc_t scan = {.wave = PET_WAVE_SCAN, .amp = -14};
    TEST_ASSERT_EQUAL(-14, pet_osc_value(&scan, 0, 0, 1000, 0));
    const pet_osc_t jit = {.wave = PET_WAVE_JITTER, .amp = -2, .period_ms = 100};
    for (uint32_t r = 0; r < 20; r++) {
        const int16_t v = pet_osc_value(&jit, 0, 0, 0, r * 7919);
        TEST_ASSERT_TRUE(v >= -2 && v <= 2);
    }
    const pet_osc_t zero = {.wave = PET_WAVE_SINE, .amp = 3, .period_ms = 0};
    TEST_ASSERT_EQUAL(0, pet_osc_value(&zero, 123, 0, 0, 0));
}

static void test_blink_range(void)
{
    uint32_t seed = 0x1234567;
    for (int m = 0; m < MOOD_COUNT; m++) {
        const pet_pose_t *p = pet_pose_for_mood((proto_mood_t)m);
        for (int i = 0; i < 2000; i++) {
            const uint32_t ms = pet_blink_next(p, prng(&seed));
            if (p->blink_min_ms == 0) {
                TEST_ASSERT_EQUAL_UINT32(0, ms);
            } else {
                TEST_ASSERT_TRUE(ms >= 2000 && ms <= 6000);
                TEST_ASSERT_TRUE(ms >= (uint32_t)p->blink_min_ms && ms <= (uint32_t)p->blink_max_ms);
            }
        }
    }
    const pet_pose_t p = {.blink_min_ms = 2000, .blink_max_ms = 6000};
    TEST_ASSERT_EQUAL_UINT32(2000, pet_blink_next(&p, 0));
    TEST_ASSERT_EQUAL_UINT32(6000, pet_blink_next(&p, 4000));
    TEST_ASSERT_EQUAL_UINT32(2000, pet_blink_next(&p, 4001));
    const pet_pose_t inverted = {.blink_min_ms = 3000, .blink_max_ms = 1000};
    TEST_ASSERT_EQUAL_UINT32(3000, pet_blink_next(&inverted, 12345));
}

static void test_glance_look_timing(void)
{
    uint32_t seed = 42;
    pet_glance_t g;
    for (int round = 0; round < 200; round++) {
        const uint32_t start = 1000u * (uint32_t)round + 0xFFFF0000u; /* also across the 32-bit wrap */
        pet_glance_init(&g, prng, &seed, start);
        pet_glance_set_mode(&g, PET_GLANCE_LOOK, start);
        const uint32_t wait = g.next_ms - start;
        TEST_ASSERT_TRUE(wait >= PET_GLANCE_MIN_MS && wait <= PET_GLANCE_MAX_MS);
        TEST_ASSERT_FALSE(pet_glance_update(&g, g.next_ms - 1));
        TEST_ASSERT_EQUAL_INT16(0, g.dx);
        TEST_ASSERT_TRUE(pet_glance_update(&g, g.next_ms));
        TEST_ASSERT_EQUAL_INT16(PET_GLANCE_RANGE, g.dx < 0 ? -g.dx : g.dx);
        TEST_ASSERT_TRUE(g.dy >= -PET_GLANCE_RANGE / 2 && g.dy <= PET_GLANCE_RANGE / 2);
        const uint32_t since = g.since_ms;
        TEST_ASSERT_FALSE(pet_glance_update(&g, since + PET_GLANCE_HOLD_MS - 1));
        TEST_ASSERT_TRUE(pet_glance_update(&g, since + PET_GLANCE_HOLD_MS));
        TEST_ASSERT_EQUAL_INT16(0, g.dx);
        TEST_ASSERT_EQUAL_INT16(0, g.dy);
        const uint32_t again = g.next_ms - (since + PET_GLANCE_HOLD_MS);
        TEST_ASSERT_TRUE(again >= PET_GLANCE_MIN_MS && again <= PET_GLANCE_MAX_MS);
    }
}

static void test_glance_extremes_and_off(void)
{
    uint32_t r = 0;
    pet_glance_t g;
    pet_glance_init(&g, fixed_rand, &r, 0);
    pet_glance_set_mode(&g, PET_GLANCE_LOOK, 0);
    TEST_ASSERT_EQUAL_UINT32(PET_GLANCE_MIN_MS, g.next_ms);
    r = PET_GLANCE_MAX_MS - PET_GLANCE_MIN_MS;
    pet_glance_set_mode(&g, PET_GLANCE_OFF, 0);
    pet_glance_set_mode(&g, PET_GLANCE_LOOK, 0);
    TEST_ASSERT_EQUAL_UINT32(PET_GLANCE_MAX_MS, g.next_ms);

    /* Off: never moves, whatever the time. */
    pet_glance_set_mode(&g, PET_GLANCE_OFF, 0);
    for (uint32_t t = 0; t < 120000; t += 100) {
        TEST_ASSERT_FALSE(pet_glance_update(&g, t));
        TEST_ASSERT_EQUAL_INT16(0, g.dx);
    }

    /* Turning glances off mid-glance re-centres at once. */
    pet_glance_set_mode(&g, PET_GLANCE_LOOK, 0);
    pet_glance_update(&g, g.next_ms);
    TEST_ASSERT_NOT_EQUAL(0, g.dx);
    pet_glance_set_mode(&g, PET_GLANCE_OFF, g.next_ms + 1);
    TEST_ASSERT_EQUAL_INT16(0, g.dx);
    TEST_ASSERT_FALSE(g.active);

    /* A missing rand gives the shortest wait. */
    pet_glance_init(&g, NULL, NULL, 100);
    pet_glance_set_mode(&g, PET_GLANCE_LOOK, 100);
    TEST_ASSERT_EQUAL_UINT32(100 + PET_GLANCE_MIN_MS, g.next_ms);
}

static void test_glance_scan_sweeps(void)
{
    uint32_t seed = 7;
    pet_glance_t g;
    pet_glance_init(&g, prng, &seed, 0);
    pet_glance_set_mode(&g, PET_GLANCE_SCAN, 0);
    TEST_ASSERT_TRUE(g.next_ms >= PET_SCAN_MIN_MS && g.next_ms <= PET_SCAN_MAX_MS);
    const uint32_t s = g.next_ms;
    pet_glance_update(&g, s);
    TEST_ASSERT_EQUAL_INT16(0, g.dx);
    pet_glance_update(&g, s + PET_SCAN_LEG_MS);
    TEST_ASSERT_EQUAL_INT16(-PET_GLANCE_RANGE, g.dx);
    pet_glance_update(&g, s + 2 * PET_SCAN_LEG_MS);
    TEST_ASSERT_EQUAL_INT16(0, g.dx);
    pet_glance_update(&g, s + 3 * PET_SCAN_LEG_MS);
    TEST_ASSERT_EQUAL_INT16(PET_GLANCE_RANGE, g.dx);
    int16_t prev = g.dx;
    for (uint32_t t = 3 * PET_SCAN_LEG_MS; t < 4 * PET_SCAN_LEG_MS; t += 25) {
        pet_glance_update(&g, s + t);
        TEST_ASSERT_TRUE(g.dx <= prev);
        TEST_ASSERT_EQUAL_INT16(0, g.dy);
        prev = g.dx;
    }
    pet_glance_update(&g, s + 4 * PET_SCAN_LEG_MS);
    TEST_ASSERT_EQUAL_INT16(0, g.dx);
    TEST_ASSERT_FALSE(g.active);
    const uint32_t wait = g.next_ms - (s + 4 * PET_SCAN_LEG_MS);
    TEST_ASSERT_TRUE(wait >= PET_SCAN_MIN_MS && wait <= PET_SCAN_MAX_MS);
}

static void test_pokes(void)
{
    pet_pokes_t p;
    pet_pokes_init(&p);
    TEST_ASSERT_FALSE(pet_pokes_add(&p, 1000));
    TEST_ASSERT_FALSE(pet_pokes_add(&p, 1500));
    TEST_ASSERT_TRUE(pet_pokes_add(&p, 2999));
    /* The count starts over after dizzy. */
    TEST_ASSERT_FALSE(pet_pokes_add(&p, 3100));
    TEST_ASSERT_FALSE(pet_pokes_add(&p, 3200));
    TEST_ASSERT_TRUE(pet_pokes_add(&p, 3300));

    /* Slow pokes never add up. */
    pet_pokes_init(&p);
    for (uint32_t t = 0; t < 20000; t += 1000) {
        TEST_ASSERT_FALSE(pet_pokes_add(&p, t));
    }
    /* Exactly the window apart: the first one has dropped out. */
    pet_pokes_init(&p);
    TEST_ASSERT_FALSE(pet_pokes_add(&p, 0));
    TEST_ASSERT_FALSE(pet_pokes_add(&p, 1000));
    TEST_ASSERT_FALSE(pet_pokes_add(&p, PET_POKE_WINDOW_MS));
    TEST_ASSERT_TRUE(pet_pokes_add(&p, PET_POKE_WINDOW_MS + 1));
    /* Across the 32-bit wrap. */
    pet_pokes_init(&p);
    TEST_ASSERT_FALSE(pet_pokes_add(&p, UINT32_MAX - 100));
    TEST_ASSERT_FALSE(pet_pokes_add(&p, UINT32_MAX));
    TEST_ASSERT_TRUE(pet_pokes_add(&p, 200));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_mood_has_a_distinct_pose);
    RUN_TEST(test_moods_match_their_description);
    RUN_TEST(test_ease_out);
    RUN_TEST(test_lerp_endpoints_and_midpoint);
    RUN_TEST(test_lerp_colour_per_channel);
    RUN_TEST(test_lid_switches_halfway_when_it_appears);
    RUN_TEST(test_channel_table_follows_the_format);
    RUN_TEST(test_overrides_apply_per_mood);
    RUN_TEST(test_reaction_timelines);
    RUN_TEST(test_sin);
    RUN_TEST(test_aux_lerp_and_oscillators);
    RUN_TEST(test_blink_range);
    RUN_TEST(test_glance_look_timing);
    RUN_TEST(test_glance_extremes_and_off);
    RUN_TEST(test_glance_scan_sweeps);
    RUN_TEST(test_pokes);
    return UNITY_END();
}
