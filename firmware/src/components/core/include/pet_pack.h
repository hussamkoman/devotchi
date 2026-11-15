#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Pet pack, format 1 (docs/architecture.md 7.2). Every number below is part of
 * the wire format: append-only, never renumbered. pet_pack.c asserts that the
 * firmware's own enums agree with them.
 */
#define PET_PACK_FMT            1
#define PET_PACK_MAX_SIZE       2048
/** Smallest size a pet_cmd may announce (protocol 14.8); a valid pack is larger. */
#define PET_PACK_MIN_SIZE       36
#define PET_PACK_HEADER_SIZE    32
#define PET_PACK_PALETTE_SIZE   56
#define PET_PACK_FACE_SIZE      12
#define PET_PACK_AUX_SIZE       64
#define PET_PACK_CRC_SIZE       4
#define PET_PACK_ID_LEN         8
#define PET_PACK_NAME_LEN       12
#define PET_PACK_MAX_PARTS      28
#define PET_PACK_MAX_OVER       48
#define PET_PACK_MAX_OSC        32
#define PET_PACK_MAX_POINTS     4
#define PET_PACK_MAX_DRIVERS    4
#define PET_PACK_AUX_COUNT      4
/** Value of the lid channel when no lid is drawn (the default); a lid is a slant of -60..60 degrees. */
#define PET_PACK_LID_OFF        1000
/** Header flags. */
#define PET_PACK_FLAG_SHIFT     0x01
/** The body keeps the base colour; the eleven table colours go to the mood role (not with shift). */
#define PET_PACK_FLAG_GLOW      0x02

/** Moods, in proto_mood_t order. */
#define PET_PACK_MOOD_HAPPY     0
#define PET_PACK_MOOD_SAD       1
#define PET_PACK_MOOD_EXCITED   2
#define PET_PACK_MOOD_ANXIOUS   3
#define PET_PACK_MOOD_SLEEPING  4
#define PET_PACK_MOOD_ANGRY     5
#define PET_PACK_MOOD_PROUD     6
#define PET_PACK_MOOD_IDLE      7
#define PET_PACK_MOOD_FOCUSED   8
#define PET_PACK_MOOD_WORKING   9
#define PET_PACK_MOOD_THINKING  10
#define PET_PACK_MOODS          11
/** Reactions: the oscillator and override `mood` numbers; aux pose rows 11..15. */
#define PET_PACK_REACT_PAT       16
#define PET_PACK_REACT_POKE      17
#define PET_PACK_REACT_DIZZY     18
#define PET_PACK_REACT_WAKE      19
#define PET_PACK_REACT_ATTENTION 20
#define PET_PACK_REACTS          5
/** Rows of pet_def_t.aux: the 11 moods, then the 5 reactions. */
#define PET_PACK_AUX_ROWS       (PET_PACK_MOODS + PET_PACK_REACTS)

/** Pose channels (pet_pose_t fields, in this order). */
#define PET_PACK_CH_EYE_OPEN      0
#define PET_PACK_CH_EYE_SIZE      1
#define PET_PACK_CH_PUPIL_SIZE    2
#define PET_PACK_CH_PUPIL_DX      3
#define PET_PACK_CH_PUPIL_DY      4
#define PET_PACK_CH_BROW_ANGLE    5
#define PET_PACK_CH_MOUTH_CURVE   6
#define PET_PACK_CH_MOUTH_OPEN    7
#define PET_PACK_CH_BOB_AMP_PX    8
#define PET_PACK_CH_BOB_PERIOD_MS 9
#define PET_PACK_CH_BLINK_MIN_MS  10
#define PET_PACK_CH_BLINK_MAX_MS  11
#define PET_PACK_CH_JITTER_PX     12
#define PET_PACK_CH_ACCESSORY     13
#define PET_PACK_CH_GLANCE        14
#define PET_PACK_CH_LID           15
#define PET_PACK_CH_BROW_L        16
#define PET_PACK_CH_BROW_R        17
#define PET_PACK_CH_BROW_LIFT     18
#define PET_PACK_CH_MOUTH_DX      19
#define PET_PACK_CH_MOUTH_W       20
#define PET_PACK_CH_COUNT         21

/** Accessories: values of channel 13 and bits of the face's acc_mask. */
#define PET_PACK_ACC_NONE       0
#define PET_PACK_ACC_ZZZ        1
#define PET_PACK_ACC_SWEAT      2
#define PET_PACK_ACC_TEAR       3
#define PET_PACK_ACC_SPARKLES   4
#define PET_PACK_ACC_DOTS       5
#define PET_PACK_ACC_TONGUE     6
#define PET_PACK_ACC_COUNT      7

/** Colour roles. */
#define PET_PACK_ROLE_BODY        0
#define PET_PACK_ROLE_DARK        1
#define PET_PACK_ROLE_LIGHT       2
#define PET_PACK_ROLE_CHEEK       3
#define PET_PACK_ROLE_ACCENT      4
#define PET_PACK_ROLE_ACCENT_DARK 5
#define PET_PACK_ROLE_CREAM       6
#define PET_PACK_ROLE_INK         7
#define PET_PACK_ROLE_WHITE       8
#define PET_PACK_ROLE_FLAME       9
#define PET_PACK_ROLE_SURFACE     10
#define PET_PACK_ROLE_FOG         11
#define PET_PACK_ROLE_YELLOW      12
/** The mood colour: the body's, except in a glow palette, where only this role takes it. */
#define PET_PACK_ROLE_MOOD        13
/** No fill colour: the part is drawn only in outline mode (the night clock), as a line of the silhouette. */
#define PET_PACK_ROLE_NIGHT       14
#define PET_PACK_ROLE_COUNT       15

typedef enum {
    PET_SHAPE_RRECT,
    PET_SHAPE_LINE,
    PET_SHAPE_TRI,
} pet_shape_t;

/** Layers back to front: behind does not bob; body and head bob; head and front follow the sink. */
typedef enum {
    PET_LAYER_BEHIND,
    PET_LAYER_BODY,
    PET_LAYER_HEAD,
    PET_LAYER_FRONT,
} pet_layer_t;

typedef enum {
    PET_DRV_MOVE_X,
    PET_DRV_MOVE_Y,
    /** Shown only while the aux channel is not 0. */
    PET_DRV_SHOW_NONZERO,
    /** Shown only while the aux channel equals k. */
    PET_DRV_SHOW_EQ,
} pet_drv_op_t;

/** Point index of a move driver that moves the whole part. */
#define PET_DRV_WHOLE  7

typedef enum {
    PET_WAVE_SINE,
    PET_WAVE_BOB,
    PET_WAVE_SCAN,
    PET_WAVE_JITTER,
} pet_wave_t;

typedef enum {
    PET_EYE_ROUND,
    PET_EYE_SLIT,
    PET_EYE_DOT,
    /** A round eye with an ink ring around the sclera. */
    PET_EYE_RING,
} pet_eye_style_t;

typedef enum {
    PET_MOUTH_PLAIN,
    PET_MOUTH_SNOUT,
} pet_mouth_set_t;

typedef struct {
    int8_t x;
    int8_t y;
} pet_pt_t;

typedef struct {
    uint8_t aux;
    uint8_t op;
    /** Point index (move; PET_DRV_WHOLE = the whole part) or k (PET_DRV_SHOW_EQ). */
    uint8_t arg;
    /** Sixteenths of a pixel per aux unit (move only). */
    int8_t scale;
} pet_driver_t;

/** Geometry in body units at scale 16/16, body centre at 0,0. A rrect keeps its centre in pts[0]. */
typedef struct {
    uint8_t shape;
    uint8_t layer;
    uint8_t role;
    bool mirrored;
    bool no_outline;
    /** rrect drawn as a ring (border only) instead of filled. */
    bool ring;
    /** rrect whose top edge follows the squish (its bottom stays on the feet). */
    bool squash;
    /** Index of an earlier line part whose last point translates this part, -1 for none. */
    int8_t anchor;
    uint8_t n_pts;
    uint8_t width;
    uint8_t w, h, r;
    pet_pt_t pts[PET_PACK_MAX_POINTS];
    uint8_t n_drv;
    pet_driver_t drv[PET_PACK_MAX_DRIVERS];
} pet_part_t;

typedef struct {
    uint8_t mood;
    uint8_t channel;
    int16_t value;
} pet_override_t;

typedef struct {
    uint8_t mood;
    uint8_t aux;
    uint8_t wave;
    int8_t amp;
    /** Milliseconds (the pack stores units of 50 ms). */
    uint16_t period_ms;
} pet_osc_t;

typedef struct {
    uint8_t eye_style;
    uint8_t mouth_set;
    bool brows;
    uint8_t eye_dx;
    int8_t eye_y;
    int8_t mouth_y;
    uint8_t mouth_hw;
    uint8_t sleep_drop;
    uint8_t acc_mask;
    /** Sixteenths of the squish that the eyes and the mouth follow. */
    uint8_t eye_sq;
    uint8_t mouth_sq;
    /** y of the sweat drop (its x is eye_dx + 37). */
    int8_t sweat_y;
} pet_face_t;

/** A decoded pack: fixed size, no pointers into the source bytes. */
typedef struct {
    uint8_t rev;
    uint16_t size;
    uint32_t crc32;
    char id[PET_PACK_ID_LEN + 1];
    char name[PET_PACK_NAME_LEN + 1];
    bool shift_palette;
    bool glow_palette;
    uint32_t base, accent, cream, sclera;
    /** Table mode: the mood colours; shift mode: the targets the base mixes towards. */
    uint32_t mood_rgb[PET_PACK_MOODS];
    uint8_t mood_t[PET_PACK_MOODS];
    pet_face_t face;
    int8_t aux[PET_PACK_AUX_ROWS][PET_PACK_AUX_COUNT];
    uint8_t n_over;
    pet_override_t over[PET_PACK_MAX_OVER];
    uint8_t n_osc;
    pet_osc_t osc[PET_PACK_MAX_OSC];
    uint8_t n_parts;
    pet_part_t parts[PET_PACK_MAX_PARTS];
} pet_def_t;

typedef enum {
    PET_PACK_OK,
    /** Shorter than a header plus a part, longer than PET_PACK_MAX_SIZE, or not the header's size. */
    PET_PACK_ERR_LENGTH,
    PET_PACK_ERR_CRC,
    PET_PACK_ERR_MAGIC,
    PET_PACK_ERR_FMT,
    PET_PACK_ERR_ID,
    PET_PACK_ERR_NAME,
    PET_PACK_ERR_COUNT,
    PET_PACK_ERR_RESERVED,
    PET_PACK_ERR_RANGE,
    PET_PACK_ERR_PART,
    PET_PACK_ERR_ANCHOR,
    PET_PACK_ERR_DRIVER,
    PET_PACK_ERR_TRUNCATED,
    PET_PACK_ERR_TRAILING,
} pet_pack_err_t;

/** CRC-32 (IEEE 802.3, as zlib's crc32). */
uint32_t pet_pack_crc32(const uint8_t *data, size_t len);

/** Checks every byte of an untrusted pack; reads nothing it has not bounds-checked. */
pet_pack_err_t pet_pack_validate(const uint8_t *bytes, size_t len);

/** Validates, then fills out (out is left zeroed on failure). */
pet_pack_err_t pet_pack_decode(const uint8_t *bytes, size_t len, pet_def_t *out);

/** Short name for logs and tests ("crc", "anchor", ...). */
const char *pet_pack_err_name(pet_pack_err_t e);

/** Protocol 14.8 error value of a validation failure: "crc" or "format". */
const char *pet_pack_err_wire(pet_pack_err_t e);

/** True when id matches [a-z0-9]{1,8}. */
bool pet_pack_id_ok(const char *id);

/** Row of pet_def_t.aux for an oscillator / override mood number (0..10, 16..20); -1 when invalid. */
int pet_pack_row(uint8_t mood);

/** Mood colour (0xRRGGBB) of mood 0..10: the table entry, or the base mixed towards the shift. */
uint32_t pet_pack_mood_color(const pet_def_t *def, unsigned mood);

/** Body colour while the mood colour is mood_color: the base in a glow palette, else the mood colour itself. */
uint32_t pet_pack_body_color(const pet_def_t *def, uint32_t mood_color);

/**
 * True for a body colour too dark for pet ink (luma under a quarter): the rig then draws the face lines
 * (mouth, brows, closed eyes, dot pupils) in the pack's cream and the lid edge in the light shade.
 */
bool pet_pack_dark(uint32_t rgb);

/** a mixed towards b by t/255 per channel, rounded. */
uint32_t pet_pack_mix(uint32_t a, uint32_t b, unsigned t);
