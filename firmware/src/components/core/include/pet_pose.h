#pragma once

#include "pet_pack.h"
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
    /** Tip of the tongue at the right mouth corner. */
    PET_ACC_TONGUE,
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
 * One face. Sizes are pixels at scale 16/16, eye_open, pupil_size and
 * mouth_open percent (pupil_size of eye_size), pupil offsets -100..100 of the
 * pupil's travel inside the eye (positive right and down), angles degrees
 * (positive lowers the inner ends: angry), mouth_curve -100 (frown) .. 100
 * (smile). blink_min_ms 0 means the pet does not blink.
 *
 * Phase-2 channels: lid is the slant of an upper lid drawn over the eye in the
 * body colour (PET_PACK_LID_OFF: no lid; the eye keeps its size and eye_open
 * says how much shows). brow_l and brow_r are added to brow_angle for the
 * left and right brow; brow_lift raises the right brow by that many pixels
 * (negative: the left one). mouth_dx moves the mouth sideways; mouth_w is its
 * half-width (0: the pet's own). Every field is an int16_t in the order of
 * the format-1 channel numbers (pet_pack.h), which pet_pose_channel() relies on.
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
    int16_t accessory;
    int16_t glance;
    int16_t lid;
    int16_t brow_l;
    int16_t brow_r;
    int16_t brow_lift;
    int16_t mouth_dx;
    int16_t mouth_w;
} pet_pose_t;

#define PET_POSE_CHANNELS  21

/** The field of a format-1 channel number, NULL for an unknown one. */
int16_t *pet_pose_channel(pet_pose_t *p, unsigned channel);

/** Returns a uniformly distributed 32-bit value. */
typedef uint32_t (*pet_rand_fn)(void *ctx);

/** Shared target pose of a mood (no pet overrides); an out-of-range mood gets the idle pose. */
const pet_pose_t *pet_pose_for_mood(proto_mood_t mood);

/** The mood's pose for a pet: the shared table with the pack's overrides for that mood (def may be NULL). */
void pet_pose_for_pet(const pet_def_t *def, proto_mood_t mood, pet_pose_t *out);

const char *pet_accessory_name(pet_accessory_t a);

/** Ease-out (quadratic) of a 0..1000 progress; values past 1000 count as 1000. */
uint16_t pet_ease_out(uint16_t t_permille);

/**
 * Pose between a and b at t_permille (0 = a, 1000 = b) along the ease-out
 * curve, every number interpolated; the accessory, the glance mode and a lid
 * that appears or disappears switch to b's halfway.
 */
void pet_pose_lerp(const pet_pose_t *a, const pet_pose_t *b, uint16_t t_permille, pet_pose_t *out);

/** Colour (0xRRGGBB) between a and b at t_permille along the ease-out curve. */
uint32_t pet_color_lerp(uint32_t a, uint32_t b, uint16_t t_permille);

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

/** amplitude * sin(degrees), integer, for any degrees (negative too). */
int32_t pet_sin(int32_t degrees, int32_t amplitude);

typedef enum {
    PET_REACT_NONE,
    /** APPROVE on home: ^ eyes, hearts, 1.5 s. */
    PET_REACT_PAT,
    /** REJECT on home: squash and squint, 0.6 s. */
    PET_REACT_POKE,
    /** Third poke within 2 s: spiral eyes, 2 s. */
    PET_REACT_DIZZY,
    /** Leaving sleep: open, yawn, blink, 1.4 s. */
    PET_REACT_WAKE,
    /** A request waits: hop, wide eyes, bang badge; loops until stopped. */
    PET_REACT_ATTENTION,
    /** Notification classes (pet_model): hop and sparkles / sink / proud with sparkles. */
    PET_REACT_POSITIVE,
    PET_REACT_NEGATIVE,
    PET_REACT_COMPLETE,
    PET_REACT_COUNT,
} pet_react_t;

typedef enum {
    PET_EYES_POSE,
    PET_EYES_ARC,
    PET_EYES_SPIRAL,
} pet_eyes_t;

typedef enum {
    PET_FX_NONE,
    PET_FX_HEARTS,
    PET_FX_IMPACT,
    PET_FX_BANG,
    /** One small z while waking. */
    PET_FX_ZZZ1,
    PET_FX_SPARKLES,
} pet_fx_t;

/** What a reaction contributes at one moment. */
typedef struct {
    bool active;
    /** 0..1000: share of the reaction's pose and aux row over the mood's. */
    uint16_t weight;
    /** 0..1000: share of the reaction pose's mouth_open (the yawn opens and closes inside the reaction). */
    uint16_t mouth;
    /** 0..1000: eye openness factor (a blink inside the reaction). */
    uint16_t lids;
    /** Pixels: positive squashes (the top comes down), negative stretches. */
    int16_t squish;
    /** Pixels the pet lifts (negative sinks). */
    int16_t hop;
    /** Pixels sideways. */
    int16_t sway;
    uint8_t eyes;
    uint8_t fx;
    /** 0..1000 progress of the effect (hearts rise, the badge pops in, the spiral turns). */
    uint16_t fx_permille;
} pet_react_frame_t;

#define PET_PAT_MS        1500
#define PET_POKE_MS       600
#define PET_DIZZY_MS      2000
#define PET_WAKE_MS       1400
#define PET_ATTENTION_MS  4000
#define PET_POSITIVE_MS   1200
#define PET_NEGATIVE_MS   600
#define PET_COMPLETE_MS   2000

const char *pet_react_name(pet_react_t r);

/** Unknown names give PET_REACT_COUNT. */
pet_react_t pet_react_parse(const char *name);

/** Length of a reaction; attention loops with this period until it is stopped. */
uint32_t pet_react_ms(pet_react_t r);

/** True when the reaction ends on its own (every one but attention). */
bool pet_react_ends(pet_react_t r);

/** The reaction elapsed_ms after it started; active is false once it is over. */
void pet_react_sample(pet_react_t r, uint32_t elapsed_ms, pet_react_frame_t *out);

/**
 * The pose a reaction blends towards: base with the reaction's channels
 * (pet-animations 3) and the pack's overrides for it. Complete is the pet's
 * proud pose; positive and negative keep base.
 */
void pet_pose_react(const pet_def_t *def, pet_react_t r, const pet_pose_t *base, pet_pose_t *out);

/** Row of pet_def_t.aux a reaction uses (complete: proud's), -1 when it keeps the mood's. */
int pet_react_aux_row(pet_react_t r);

/** Reaction number of oscillators and overrides (PET_PACK_REACT_*), 0 when it has none. */
uint8_t pet_react_pack_id(pet_react_t r);

/**
 * Aux channels between a and b at t_permille (ease-out): cur is interpolated
 * (what move drivers use), sel switches to b's halfway (what show drivers use).
 */
void pet_aux_lerp(const int16_t a[PET_PACK_AUX_COUNT], const int16_t b[PET_PACK_AUX_COUNT], uint16_t t_permille,
                  int16_t cur[PET_PACK_AUX_COUNT], int16_t sel[PET_PACK_AUX_COUNT]);

/**
 * One oscillator's offset at now_ms. bob_permille and scan_permille are the
 * bob's and the scan's position (-1000..1000). Jitter takes rand and is
 * meant to be resampled once per period by the caller.
 */
int16_t pet_osc_value(const pet_osc_t *o, uint32_t now_ms, int16_t bob_permille, int16_t scan_permille,
                      uint32_t rand);
