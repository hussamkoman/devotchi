#pragma once

#include "proto.h"

#include <stdbool.h>
#include <stdint.h>

/** Length of one blink and of the tween between two moods. */
#define PET_BLINK_MS          120
#define PET_TWEEN_MS          300
/** A glance (pupils off centre) starts every PET_GLANCE_MIN_MS..PET_GLANCE_MAX_MS and lasts PET_GLANCE_HOLD_MS. */
#define PET_GLANCE_MIN_MS     8000
#define PET_GLANCE_MAX_MS     15000
#define PET_GLANCE_HOLD_MS    900
/** Scanning eyes sweep left, right and back to centre, one leg each, every PET_SCAN_MIN_MS..PET_SCAN_MAX_MS. */
#define PET_SCAN_MIN_MS       2000
#define PET_SCAN_MAX_MS       4000
#define PET_SCAN_LEG_MS       500
/** Pupil offset of a glance, in the units of pet_pose_t.pupil_dx. */
#define PET_GLANCE_RANGE      70
/** Three pokes within this window make the pet dizzy. */
#define PET_POKE_WINDOW_MS    2000
#define PET_POKES_FOR_DIZZY   3

typedef enum {
    PET_ACC_NONE,
    PET_ACC_ZZZ,
    PET_ACC_SWEAT,
    PET_ACC_TEAR,
    PET_ACC_SPARKLES,
    PET_ACC_DOTS,
    PET_ACC_COUNT,
} pet_accessory_t;

typedef enum {
    /** Pupils stay where the pose puts them. */
    PET_GLANCE_OFF,
    /** Now and then a short look to one side. */
    PET_GLANCE_LOOK,
    /** Regular left-right sweeps, as if reading. */
    PET_GLANCE_SCAN,
} pet_glance_mode_t;

/**
 * One face. Sizes are pixels, eye_open, pupil_size and mouth_open percent
 * (pupil_size of eye_size), pupil offsets
 * -100..100 of the pupil's travel inside the eye (positive right and down),
 * brow_angle degrees (positive lowers the inner ends: angry), mouth_curve
 * -100 (frown) .. 100 (smile). blink_min_ms 0 means the pet does not blink.
 */
typedef struct {
    int16_t eye_open;
    int16_t eye_size;
    int16_t pupil_size;
    int16_t pupil_dx;
    int16_t pupil_dy;
    int16_t brow_angle;
    int16_t mouth_curve;
    int16_t mouth_open;
    int16_t bob_amp_px;
    int16_t bob_period_ms;
    int16_t blink_min_ms;
    int16_t blink_max_ms;
    int16_t jitter_px;
    uint32_t body_color;
    uint8_t accessory;
    uint8_t glance;
} pet_pose_t;

/** Returns a uniformly distributed 32-bit value. */
typedef uint32_t (*pet_rand_fn)(void *ctx);

/** Target pose of a mood; an out-of-range mood gets the idle pose. */
const pet_pose_t *pet_pose_for_mood(proto_mood_t mood);

const char *pet_accessory_name(pet_accessory_t a);

/** Ease-out (quadratic) of a 0..1000 progress; values past 1000 count as 1000. */
uint16_t pet_ease_out(uint16_t t_permille);

/**
 * Pose between a and b at t_permille (0 = a, 1000 = b) along the ease-out
 * curve, every number and colour channel interpolated; the accessory and the
 * glance mode switch to b's halfway.
 */
void pet_pose_lerp(const pet_pose_t *a, const pet_pose_t *b, uint16_t t_permille, pet_pose_t *out);

/** Milliseconds to the next blink, in [blink_min_ms, blink_max_ms]; 0 when the pose does not blink. */
uint32_t pet_blink_next(const pet_pose_t *p, uint32_t rand);

typedef struct {
    pet_glance_mode_t mode;
    /** Start of the next glance or sweep. */
    uint32_t next_ms;
    /** While active: start of the current one. */
    uint32_t since_ms;
    bool active;
    int16_t target_dx;
    int16_t target_dy;
    /** Pupil offset to add to the pose. */
    int16_t dx;
    int16_t dy;
    pet_rand_fn rand;
    void *rand_ctx;
} pet_glance_t;

void pet_glance_init(pet_glance_t *g, pet_rand_fn rand, void *rand_ctx, uint32_t now_ms);

/** Follows a mode change: a new mode drops the running glance and schedules the next from now. */
void pet_glance_set_mode(pet_glance_t *g, pet_glance_mode_t mode, uint32_t now_ms);

/** Advances the scheduler to now_ms and updates dx/dy; returns true when they changed. */
bool pet_glance_update(pet_glance_t *g, uint32_t now_ms);

typedef struct {
    uint32_t times[PET_POKES_FOR_DIZZY];
    uint8_t count;
} pet_pokes_t;

void pet_pokes_init(pet_pokes_t *p);

/** Records a poke; returns true when it is the third within PET_POKE_WINDOW_MS (the count then starts over). */
bool pet_pokes_add(pet_pokes_t *p, uint32_t now_ms);
