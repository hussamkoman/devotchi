#include "pet_pose.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(pet_pose_t) == PET_POSE_CHANNELS * sizeof(int16_t), "pet_pose_t must be the channel array");

/* The shared poses (pet-animations 1 and 3). Idle, focused, working and thinking
   are the warmer phase-2 faces; the other seven keep their phase-1 numbers. */
#define BASE(eo, es, ps, dx, dy, brow, curve, open, bob, bob_ms, bmin, bmax, jit, acc, look, lid_)                    \
    .eye_open = (eo), .eye_size = (es), .pupil_size = (ps), .pupil_dx = (dx), .pupil_dy = (dy), .brow_angle = (brow),  \
    .mouth_curve = (curve), .mouth_open = (open), .bob_amp_px = (bob), .bob_period_ms = (bob_ms),                      \
    .blink_min_ms = (bmin), .blink_max_ms = (bmax), .jitter_px = (jit), .accessory = (acc), .glance = (look),          \
    .lid = (lid_)
#define NO_LID PET_PACK_LID_OFF

static const pet_pose_t POSES[MOOD_COUNT] = {
    [MOOD_HAPPY] = {BASE(100, 26, 45, 0, 0, -5, 80, 0, 3, 2400, 2000, 6000, 0, PET_ACC_NONE, PET_GLANCE_LOOK, NO_LID)},
    [MOOD_SAD] = {BASE(65, 24, 45, 0, 40, -20, -70, 0, 1, 4000, 2500, 6000, 0, PET_ACC_TEAR, PET_GLANCE_LOOK, NO_LID)},
    [MOOD_EXCITED] = {BASE(100, 32, 40, 0, 0, -12, 100, 60, 5, 800, 3000, 6000, 0, PET_ACC_SPARKLES, PET_GLANCE_LOOK, NO_LID)},
    [MOOD_ANXIOUS] = {BASE(100, 28, 22, 0, 0, -18, -25, 15, 1, 1500, 2000, 3000, 1, PET_ACC_SWEAT, PET_GLANCE_LOOK, NO_LID)},
    [MOOD_SLEEPING] = {BASE(0, 26, 45, 0, 0, 0, 15, 0, 3, 5000, 0, 0, 0, PET_ACC_ZZZ, PET_GLANCE_OFF, NO_LID)},
    [MOOD_ANGRY] = {BASE(60, 26, 50, 0, 0, 28, -55, 0, 1, 1200, 2500, 6000, 0, PET_ACC_NONE, PET_GLANCE_OFF, NO_LID)},
    [MOOD_PROUD] = {BASE(65, 26, 45, 0, -45, -8, 65, 0, 2, 3000, 2500, 6000, 0, PET_ACC_SPARKLES, PET_GLANCE_OFF, NO_LID)},
    [MOOD_IDLE] = {BASE(100, 25, 46, 0, 0, 0, 42, 0, 2, 3500, 2000, 6000, 0, PET_ACC_NONE, PET_GLANCE_LOOK, NO_LID)},
    [MOOD_FOCUSED] = {BASE(62, 26, 52, 0, 20, 0, 16, 0, 1, 4000, 4000, 6000, 0, PET_ACC_NONE, PET_GLANCE_OFF, 0),
                      .mouth_w = 11},
    [MOOD_WORKING] = {BASE(88, 24, 46, 0, 45, 0, 34, 0, 2, 2000, 3000, 6000, 0, PET_ACC_TONGUE, PET_GLANCE_SCAN, NO_LID),
                      .mouth_w = 13},
    [MOOD_THINKING] = {BASE(92, 24, 45, 60, -60, 0, -4, 0, 1, 3000, 3000, 6000, 0, PET_ACC_DOTS, PET_GLANCE_OFF, NO_LID),
                       .brow_l = -4, .brow_r = -16, .brow_lift = 5, .mouth_dx = 9, .mouth_w = 8},
};

static const char *const ACCESSORY_NAMES[PET_ACC_COUNT] = {
    [PET_ACC_NONE] = "none",         [PET_ACC_ZZZ] = "zzz",   [PET_ACC_SWEAT] = "sweat",   [PET_ACC_TEAR] = "tear",
    [PET_ACC_SPARKLES] = "sparkles", [PET_ACC_DOTS] = "dots", [PET_ACC_TONGUE] = "tongue",
};

int16_t *pet_pose_channel(pet_pose_t *p, unsigned channel)
{
    return channel < PET_POSE_CHANNELS ? &p->eye_open + channel : NULL;
}

const pet_pose_t *pet_pose_for_mood(proto_mood_t mood)
{
    return &POSES[(unsigned)mood < MOOD_COUNT ? mood : MOOD_IDLE];
}

static void apply_overrides(const pet_def_t *def, uint8_t mood, pet_pose_t *out)
{
    if (def == NULL) {
        return;
    }
    for (uint8_t i = 0; i < def->n_over && i < PET_PACK_MAX_OVER; i++) {
        if (def->over[i].mood == mood) {
            int16_t *ch = pet_pose_channel(out, def->over[i].channel);
            if (ch != NULL) {
                *ch = def->over[i].value;
            }
        }
    }
}

void pet_pose_for_pet(const pet_def_t *def, proto_mood_t mood, pet_pose_t *out)
{
    if ((unsigned)mood >= MOOD_COUNT) {
        mood = MOOD_IDLE;
    }
    *out = POSES[mood];
    apply_overrides(def, (uint8_t)mood, out);
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
    const bool late = t_permille >= 500;
    pet_pose_t r;
    const int16_t *pa = &a->eye_open;
    const int16_t *pb = &b->eye_open;
    int16_t *pr = &r.eye_open;
    for (unsigned ch = 0; ch < PET_POSE_CHANNELS; ch++) {
        pr[ch] = mix(pa[ch], pb[ch], e);
    }
    r.accessory = late ? b->accessory : a->accessory;
    r.glance = late ? b->glance : a->glance;
    if (a->lid == PET_PACK_LID_OFF || b->lid == PET_PACK_LID_OFF) {
        r.lid = late ? b->lid : a->lid;
    }
    *out = r;
}

uint32_t pet_color_lerp(uint32_t a, uint32_t b, uint16_t t_permille)
{
    return mix_color(a, b, pet_ease_out(t_permille));
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

int32_t pet_sin(int32_t degrees, int32_t amplitude)
{
    int32_t d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    const int32_t sign = d >= 180 ? -1 : 1;
    if (d >= 180) {
        d -= 180;
    }
    /* Bhaskara I: within 0.2 % of sin over 0..180 degrees. */
    const int32_t p = d * (180 - d);
    return sign * (4 * p * amplitude) / (40500 - p);
}

static const char *const REACT_NAMES[PET_REACT_COUNT] = {
    [PET_REACT_NONE] = "none",         [PET_REACT_PAT] = "pat",           [PET_REACT_POKE] = "poke",
    [PET_REACT_DIZZY] = "dizzy",       [PET_REACT_WAKE] = "wake",         [PET_REACT_ATTENTION] = "attention",
    [PET_REACT_POSITIVE] = "positive", [PET_REACT_NEGATIVE] = "negative", [PET_REACT_COMPLETE] = "complete",
};

static const uint32_t REACT_MS[PET_REACT_COUNT] = {
    [PET_REACT_PAT] = PET_PAT_MS,           [PET_REACT_POKE] = PET_POKE_MS,
    [PET_REACT_DIZZY] = PET_DIZZY_MS,       [PET_REACT_WAKE] = PET_WAKE_MS,
    [PET_REACT_ATTENTION] = PET_ATTENTION_MS, [PET_REACT_POSITIVE] = PET_POSITIVE_MS,
    [PET_REACT_NEGATIVE] = PET_NEGATIVE_MS, [PET_REACT_COMPLETE] = PET_COMPLETE_MS,
};

const char *pet_react_name(pet_react_t r)
{
    return (unsigned)r < PET_REACT_COUNT ? REACT_NAMES[r] : "none";
}

pet_react_t pet_react_parse(const char *name)
{
    for (int r = 0; r < PET_REACT_COUNT; r++) {
        if (strcmp(name, REACT_NAMES[r]) == 0) {
            return (pet_react_t)r;
        }
    }
    return PET_REACT_COUNT;
}

uint32_t pet_react_ms(pet_react_t r)
{
    return (unsigned)r < PET_REACT_COUNT ? REACT_MS[r] : 0;
}

bool pet_react_ends(pet_react_t r)
{
    return r != PET_REACT_ATTENTION;
}

/* Ease-out progress 0..1000 of t through [t0, t1). */
static uint16_t ramp(uint32_t t, uint32_t t0, uint32_t t1)
{
    if (t <= t0) {
        return 0;
    }
    if (t >= t1) {
        return 1000;
    }
    return pet_ease_out((uint16_t)((t - t0) * 1000 / (t1 - t0)));
}

/* In over [0, in), held, out over [out0, end). */
static uint16_t envelope(uint32_t t, uint32_t in, uint32_t out0, uint32_t end)
{
    return t < out0 ? ramp(t, 0, in) : (uint16_t)(1000 - ramp(t, out0, end));
}

/* A blink of length ms starting at t0: the eye factor goes 1000 -> 0 -> 1000. */
static uint16_t blink_at(uint32_t t, uint32_t t0, uint32_t ms)
{
    if (t < t0 || t >= t0 + ms) {
        return 1000;
    }
    const int32_t half = (int32_t)ms / 2;
    const int32_t d = (int32_t)(t - t0) - half;
    return (uint16_t)((d < 0 ? -d : d) * 1000 / half);
}

void pet_react_sample(pet_react_t r, uint32_t t, pet_react_frame_t *out)
{
    *out = (pet_react_frame_t){.lids = 1000};
    if (r == PET_REACT_NONE || (unsigned)r >= PET_REACT_COUNT || (pet_react_ends(r) && t >= REACT_MS[r])) {
        return;
    }
    out->active = true;
    out->mouth = 1000;
    switch (r) {
    case PET_REACT_PAT:
        out->weight = envelope(t, 150, 1200, PET_PAT_MS);
        out->eyes = t < 1350 ? PET_EYES_ARC : PET_EYES_POSE;
        out->fx = PET_FX_HEARTS;
        out->fx_permille = (uint16_t)(t * 1000 / PET_PAT_MS);
        break;
    case PET_REACT_POKE:
        out->weight = envelope(t, 40, 400, PET_POKE_MS);
        out->squish = (int16_t)(t < 80 ? 12 * ramp(t, 0, 80) / 1000 : 12 * (1000 - ramp(t, 80, 400)) / 1000);
        out->fx = t < 200 ? PET_FX_IMPACT : PET_FX_NONE;
        out->fx_permille = (uint16_t)(t < 200 ? t * 5 : 1000);
        break;
    case PET_REACT_DIZZY:
        out->weight = envelope(t, 150, 1700, PET_DIZZY_MS);
        out->eyes = t < 1850 ? PET_EYES_SPIRAL : PET_EYES_POSE;
        out->fx_permille = (uint16_t)(t % 1000);
        out->sway = (int16_t)pet_sin((int32_t)(t * 360 / 500), 2);
        break;
    case PET_REACT_WAKE:
        out->weight = envelope(t, 300, 1000, PET_WAKE_MS);
        out->mouth = t < 800 ? ramp(t, 300, 550) : (uint16_t)(1000 - ramp(t, 800, 1000));
        out->squish = (int16_t)(-8 * (int32_t)out->mouth / 1000);
        out->lids = blink_at(t, 800, 200);
        out->fx = t < 300 ? PET_FX_ZZZ1 : PET_FX_NONE;
        break;
    case PET_REACT_ATTENTION: {
        const uint32_t c = t % PET_ATTENTION_MS;
        const bool first = t < PET_ATTENTION_MS;
        out->weight = first ? ramp(t, 0, 150) : 1000;
        out->hop = (int16_t)(c < 150 ? 6 * ramp(c, 0, 150) / 1000 : 6 * (1000 - ramp(c, 150, 300)) / 1000);
        out->lids = blink_at(c, 2000, PET_BLINK_MS);
        if (!first || c >= 300) {
            out->fx = PET_FX_BANG;
            out->fx_permille = first ? ramp(c, 300, 450) : 1000;
        }
        break;
    }
    case PET_REACT_POSITIVE:
        out->hop = (int16_t)(t < 150 ? 5 * ramp(t, 0, 150) / 1000 : 5 * (1000 - ramp(t, 150, 400)) / 1000);
        out->fx = PET_FX_SPARKLES;
        out->fx_permille = (uint16_t)(t * 1000 / PET_POSITIVE_MS);
        break;
    case PET_REACT_NEGATIVE:
        out->hop = (int16_t)(t < 300 ? -3 * ramp(t, 0, 100) / 1000 : -3 * (1000 - ramp(t, 300, PET_NEGATIVE_MS)) / 1000);
        out->weight = envelope(t, 100, 300, PET_NEGATIVE_MS);
        break;
    case PET_REACT_COMPLETE:
        out->weight = envelope(t, 300, 1700, PET_COMPLETE_MS);
        out->fx = PET_FX_SPARKLES;
        out->fx_permille = (uint16_t)(t * 1000 / PET_COMPLETE_MS);
        break;
    case PET_REACT_NONE:
    case PET_REACT_COUNT:
        break;
    }
}

typedef struct {
    uint8_t ch;
    int16_t value;
} react_ch_t;

#define REACT_CHANNELS  12

/* pet-animations 3: the channels each reaction sets; the rest stay the mood's. */
static const react_ch_t REACT_POSE[PET_REACT_COUNT][REACT_CHANNELS] = {
    [PET_REACT_PAT] = {{PET_PACK_CH_MOUTH_CURVE, 90}, {PET_PACK_CH_MOUTH_OPEN, 0}, {PET_PACK_CH_BROW_ANGLE, 0},
                       {PET_PACK_CH_ACCESSORY, PET_ACC_NONE}, {PET_PACK_CH_MOUTH_DX, 0}, {PET_PACK_CH_MOUTH_W, 0},
                       {PET_PACK_CH_GLANCE, PET_GLANCE_OFF}},
    [PET_REACT_POKE] = {{PET_PACK_CH_EYE_OPEN, 25}, {PET_PACK_CH_MOUTH_CURVE, -20}, {PET_PACK_CH_MOUTH_OPEN, 0},
                        {PET_PACK_CH_BROW_ANGLE, -14}, {PET_PACK_CH_LID, PET_PACK_LID_OFF},
                        {PET_PACK_CH_ACCESSORY, PET_ACC_NONE}, {PET_PACK_CH_MOUTH_DX, 0},
                        {PET_PACK_CH_GLANCE, PET_GLANCE_OFF}},
    [PET_REACT_DIZZY] = {{PET_PACK_CH_MOUTH_CURVE, -10}, {PET_PACK_CH_MOUTH_OPEN, 20}, {PET_PACK_CH_BROW_ANGLE, 0},
                         {PET_PACK_CH_ACCESSORY, PET_ACC_NONE}, {PET_PACK_CH_MOUTH_DX, 0},
                         {PET_PACK_CH_GLANCE, PET_GLANCE_OFF}},
    [PET_REACT_WAKE] = {{PET_PACK_CH_EYE_OPEN, 38}, {PET_PACK_CH_MOUTH_CURVE, 0}, {PET_PACK_CH_MOUTH_OPEN, 70},
                        {PET_PACK_CH_BROW_ANGLE, -16}, {PET_PACK_CH_EYE_SIZE, 26}, {PET_PACK_CH_LID, PET_PACK_LID_OFF},
                        {PET_PACK_CH_ACCESSORY, PET_ACC_NONE}, {PET_PACK_CH_MOUTH_DX, 0},
                        {PET_PACK_CH_GLANCE, PET_GLANCE_OFF}, {PET_PACK_CH_BLINK_MIN_MS, 0}},
    [PET_REACT_ATTENTION] = {{PET_PACK_CH_EYE_OPEN, 100}, {PET_PACK_CH_EYE_SIZE, 30}, {PET_PACK_CH_PUPIL_SIZE, 34},
                             {PET_PACK_CH_PUPIL_DY, -30}, {PET_PACK_CH_PUPIL_DX, 0}, {PET_PACK_CH_MOUTH_CURVE, 0},
                             {PET_PACK_CH_MOUTH_OPEN, 22}, {PET_PACK_CH_BROW_ANGLE, -14},
                             {PET_PACK_CH_LID, PET_PACK_LID_OFF}, {PET_PACK_CH_ACCESSORY, PET_ACC_NONE},
                             {PET_PACK_CH_MOUTH_DX, 0}, {PET_PACK_CH_GLANCE, PET_GLANCE_OFF}},
};

/* Unused slots of REACT_POSE are {0, 0}, which would set eye_open to 0: they are skipped by count. */
static const uint8_t REACT_POSE_N[PET_REACT_COUNT] = {
    [PET_REACT_PAT] = 7, [PET_REACT_POKE] = 8, [PET_REACT_DIZZY] = 6, [PET_REACT_WAKE] = 10, [PET_REACT_ATTENTION] = 12,
};

uint8_t pet_react_pack_id(pet_react_t r)
{
    switch (r) {
    case PET_REACT_PAT:
        return PET_PACK_REACT_PAT;
    case PET_REACT_POKE:
        return PET_PACK_REACT_POKE;
    case PET_REACT_DIZZY:
        return PET_PACK_REACT_DIZZY;
    case PET_REACT_WAKE:
        return PET_PACK_REACT_WAKE;
    case PET_REACT_ATTENTION:
        return PET_PACK_REACT_ATTENTION;
    default:
        return 0;
    }
}

int pet_react_aux_row(pet_react_t r)
{
    if (r == PET_REACT_COMPLETE) {
        return MOOD_PROUD;
    }
    const uint8_t id = pet_react_pack_id(r);
    return id != 0 ? pet_pack_row(id) : -1;
}

void pet_pose_react(const pet_def_t *def, pet_react_t r, const pet_pose_t *base, pet_pose_t *out)
{
    if (r == PET_REACT_COMPLETE) {
        pet_pose_for_pet(def, MOOD_PROUD, out);
        return;
    }
    *out = *base;
    if ((unsigned)r >= PET_REACT_COUNT) {
        return;
    }
    out->brow_l = 0;
    out->brow_r = 0;
    out->brow_lift = 0;
    for (uint8_t i = 0; i < REACT_POSE_N[r]; i++) {
        *pet_pose_channel(out, REACT_POSE[r][i].ch) = REACT_POSE[r][i].value;
    }
    const uint8_t id = pet_react_pack_id(r);
    if (id != 0) {
        apply_overrides(def, id, out);
    }
}

void pet_aux_lerp(const int16_t a[PET_PACK_AUX_COUNT], const int16_t b[PET_PACK_AUX_COUNT], uint16_t t_permille,
                  int16_t cur[PET_PACK_AUX_COUNT], int16_t sel[PET_PACK_AUX_COUNT])
{
    const int32_t e = pet_ease_out(t_permille);
    for (int i = 0; i < PET_PACK_AUX_COUNT; i++) {
        cur[i] = mix(a[i], b[i], e);
        sel[i] = t_permille >= 500 ? b[i] : a[i];
    }
}

int16_t pet_osc_value(const pet_osc_t *o, uint32_t now_ms, int16_t bob_permille, int16_t scan_permille,
                      uint32_t rand)
{
    switch (o->wave) {
    case PET_WAVE_SINE:
        if (o->period_ms == 0) {
            return 0;
        }
        return (int16_t)pet_sin((int32_t)((now_ms % o->period_ms) * 360 / o->period_ms), o->amp);
    case PET_WAVE_BOB:
        return (int16_t)(o->amp * bob_permille / 1000);
    case PET_WAVE_SCAN:
        return (int16_t)(o->amp * scan_permille / 1000);
    case PET_WAVE_JITTER: {
        const int32_t amp = o->amp < 0 ? -o->amp : o->amp;
        return (int16_t)((int32_t)(rand % (uint32_t)(2 * amp + 1)) - amp);
    }
    default:
        return 0;
    }
}
