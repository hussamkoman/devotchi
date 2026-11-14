#include "pet_pose.h"

#include <stddef.h>

/* eye_open, eye_size, pupil_size, pupil_dx, pupil_dy, brow_angle, mouth_curve, mouth_open,
   bob_amp_px, bob_period_ms, blink_min_ms, blink_max_ms, jitter_px, body_color, accessory, glance */
static const pet_pose_t POSES[MOOD_COUNT] = {
    [MOOD_HAPPY] = {100, 26, 45, 0, 0, -5, 80, 0, 3, 2400, 2000, 6000, 0, 0xFFB74D, PET_ACC_NONE, PET_GLANCE_LOOK},
    [MOOD_SAD] = {65, 24, 45, 0, 40, -20, -70, 0, 1, 4000, 2500, 6000, 0, 0x64B5F6, PET_ACC_TEAR, PET_GLANCE_LOOK},
    [MOOD_EXCITED] = {100, 32, 40, 0, 0, -12, 100, 60, 5, 800, 3000, 6000, 0, 0xFFE082, PET_ACC_SPARKLES,
                      PET_GLANCE_LOOK},
    [MOOD_ANXIOUS] = {100, 28, 22, 0, 0, -18, -25, 15, 1, 1500, 2000, 3000, 1, 0xCE93D8, PET_ACC_SWEAT,
                      PET_GLANCE_LOOK},
    [MOOD_SLEEPING] = {0, 26, 45, 0, 0, 0, 15, 0, 3, 5000, 0, 0, 0, 0x7986CB, PET_ACC_ZZZ, PET_GLANCE_OFF},
    [MOOD_ANGRY] = {60, 26, 50, 0, 0, 28, -55, 0, 1, 1200, 2500, 6000, 0, 0xE57373, PET_ACC_NONE, PET_GLANCE_OFF},
    [MOOD_PROUD] = {65, 26, 45, 0, -45, -8, 65, 0, 2, 3000, 2500, 6000, 0, 0x4DB6AC, PET_ACC_SPARKLES,
                    PET_GLANCE_OFF},
    [MOOD_IDLE] = {100, 24, 45, 0, 0, 0, 20, 0, 2, 3500, 2000, 6000, 0, 0x90A4AE, PET_ACC_NONE, PET_GLANCE_LOOK},
    [MOOD_FOCUSED] = {40, 26, 50, 0, 0, 12, 0, 0, 1, 4000, 4000, 6000, 0, 0x4FC3F7, PET_ACC_NONE, PET_GLANCE_OFF},
    [MOOD_WORKING] = {80, 24, 45, 0, 15, 6, 10, 0, 2, 2000, 3000, 6000, 0, 0x81C784, PET_ACC_NONE, PET_GLANCE_SCAN},
    [MOOD_THINKING] = {90, 24, 45, 60, -60, -10, -5, 0, 1, 3000, 3000, 6000, 0, 0x9575CD, PET_ACC_DOTS,
                       PET_GLANCE_OFF},
};

static const char *const ACCESSORY_NAMES[PET_ACC_COUNT] = {
    [PET_ACC_NONE] = "none",   [PET_ACC_ZZZ] = "zzz",           [PET_ACC_SWEAT] = "sweat",
    [PET_ACC_TEAR] = "tear",   [PET_ACC_SPARKLES] = "sparkles", [PET_ACC_DOTS] = "dots",
};

const pet_pose_t *pet_pose_for_mood(proto_mood_t mood)
{
    return &POSES[(unsigned)mood < MOOD_COUNT ? mood : MOOD_IDLE];
}

const char *pet_accessory_name(pet_accessory_t a)
{
    return (unsigned)a < PET_ACC_COUNT ? ACCESSORY_NAMES[a] : "none";
}

uint16_t pet_ease_out(uint16_t t_permille)
{
    const uint32_t t = t_permille > 1000 ? 1000 : t_permille;
    const uint32_t rest = 1000 - t;
    return (uint16_t)(1000 - rest * rest / 1000);
}

static int16_t mix(int16_t a, int16_t b, int32_t e)
{
    return (int16_t)(a + ((int32_t)b - a) * e / 1000);
}

static uint32_t mix_color(uint32_t a, uint32_t b, int32_t e)
{
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int32_t ca = (int32_t)((a >> shift) & 0xFF);
        const int32_t cb = (int32_t)((b >> shift) & 0xFF);
        out |= (uint32_t)(ca + (cb - ca) * e / 1000) << shift;
    }
    return out;
}

void pet_pose_lerp(const pet_pose_t *a, const pet_pose_t *b, uint16_t t_permille, pet_pose_t *out)
{
    const int32_t e = pet_ease_out(t_permille);
    pet_pose_t r = {
        .eye_open = mix(a->eye_open, b->eye_open, e),
        .eye_size = mix(a->eye_size, b->eye_size, e),
        .pupil_size = mix(a->pupil_size, b->pupil_size, e),
        .pupil_dx = mix(a->pupil_dx, b->pupil_dx, e),
        .pupil_dy = mix(a->pupil_dy, b->pupil_dy, e),
        .brow_angle = mix(a->brow_angle, b->brow_angle, e),
        .mouth_curve = mix(a->mouth_curve, b->mouth_curve, e),
        .mouth_open = mix(a->mouth_open, b->mouth_open, e),
        .bob_amp_px = mix(a->bob_amp_px, b->bob_amp_px, e),
        .bob_period_ms = mix(a->bob_period_ms, b->bob_period_ms, e),
        .blink_min_ms = mix(a->blink_min_ms, b->blink_min_ms, e),
        .blink_max_ms = mix(a->blink_max_ms, b->blink_max_ms, e),
        .jitter_px = mix(a->jitter_px, b->jitter_px, e),
        .body_color = mix_color(a->body_color, b->body_color, e),
        .accessory = t_permille >= 500 ? b->accessory : a->accessory,
        .glance = t_permille >= 500 ? b->glance : a->glance,
    };
    *out = r;
}

uint32_t pet_blink_next(const pet_pose_t *p, uint32_t rand)
{
    if (p->blink_min_ms <= 0) {
        return 0;
    }
    const uint32_t min = (uint32_t)p->blink_min_ms;
    const uint32_t max = p->blink_max_ms > p->blink_min_ms ? (uint32_t)p->blink_max_ms : min;
    return min + rand % (max - min + 1);
}

static uint32_t between(pet_glance_t *g, uint32_t min, uint32_t max)
{
    const uint32_t r = g->rand != NULL ? g->rand(g->rand_ctx) : 0;
    return min + r % (max - min + 1);
}

static void schedule(pet_glance_t *g, uint32_t now_ms)
{
    g->active = false;
    if (g->mode == PET_GLANCE_SCAN) {
        g->next_ms = now_ms + between(g, PET_SCAN_MIN_MS, PET_SCAN_MAX_MS);
    } else {
        g->next_ms = now_ms + between(g, PET_GLANCE_MIN_MS, PET_GLANCE_MAX_MS);
    }
}

void pet_glance_init(pet_glance_t *g, pet_rand_fn rand, void *rand_ctx, uint32_t now_ms)
{
    *g = (pet_glance_t){.mode = PET_GLANCE_OFF, .rand = rand, .rand_ctx = rand_ctx};
    schedule(g, now_ms);
}

void pet_glance_set_mode(pet_glance_t *g, pet_glance_mode_t mode, uint32_t now_ms)
{
    if (mode == g->mode) {
        return;
    }
    g->mode = mode;
    g->dx = 0;
    g->dy = 0;
    schedule(g, now_ms);
}

static bool reached(uint32_t at, uint32_t now_ms)
{
    return (int32_t)(now_ms - at) >= 0;
}

/* Offset of a sweep t ms after its start: left, then right, then back to centre, one leg each. */
static int16_t scan_dx(uint32_t t)
{
    const int32_t leg = PET_SCAN_LEG_MS;
    const int32_t range = PET_GLANCE_RANGE;
    const int32_t x = (int32_t)t;
    if (x < leg) {
        return (int16_t)(-range * x / leg);
    }
    if (x < 3 * leg) {
        return (int16_t)(-range + 2 * range * (x - leg) / (2 * leg));
    }
    return (int16_t)(range - range * (x - 3 * leg) / leg);
}

bool pet_glance_update(pet_glance_t *g, uint32_t now_ms)
{
    const int16_t old_dx = g->dx;
    const int16_t old_dy = g->dy;
    if (g->mode == PET_GLANCE_OFF) {
        g->dx = 0;
        g->dy = 0;
    } else if (!g->active) {
        if (reached(g->next_ms, now_ms)) {
            g->active = true;
            g->since_ms = now_ms;
            if (g->mode == PET_GLANCE_LOOK) {
                const uint32_t r = g->rand != NULL ? g->rand(g->rand_ctx) : 0;
                g->target_dx = (int16_t)((r & 1) ? PET_GLANCE_RANGE : -PET_GLANCE_RANGE);
                g->target_dy = (int16_t)((int32_t)((r >> 1) % 3) * (PET_GLANCE_RANGE / 2) - PET_GLANCE_RANGE / 2);
                g->dx = g->target_dx;
                g->dy = g->target_dy;
            } else {
                g->dx = 0;
                g->dy = 0;
            }
        }
    } else {
        const uint32_t t = now_ms - g->since_ms;
        const uint32_t length = g->mode == PET_GLANCE_LOOK ? PET_GLANCE_HOLD_MS : 4 * PET_SCAN_LEG_MS;
        if (t >= length) {
            g->dx = 0;
            g->dy = 0;
            schedule(g, now_ms);
        } else if (g->mode == PET_GLANCE_SCAN) {
            g->dx = scan_dx(t);
            g->dy = 0;
        }
    }
    return g->dx != old_dx || g->dy != old_dy;
}

void pet_pokes_init(pet_pokes_t *p)
{
    *p = (pet_pokes_t){0};
}

bool pet_pokes_add(pet_pokes_t *p, uint32_t now_ms)
{
    /* Forget pokes that fell out of the window. */
    uint8_t kept = 0;
    for (uint8_t i = 0; i < p->count; i++) {
        if (now_ms - p->times[i] < PET_POKE_WINDOW_MS) {
            p->times[kept++] = p->times[i];
        }
    }
    p->count = kept;
    p->times[p->count++] = now_ms;
    if (p->count >= PET_POKES_FOR_DIZZY) {
        p->count = 0;
        return true;
    }
    return false;
}
