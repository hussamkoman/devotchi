#include "pet_pack.h"

#include "pet_pose.h"
#include "proto.h"

#include <string.h>

/* The wire numbers are fixed; a reordered firmware enum must map, not renumber. */
_Static_assert(PET_PACK_MOOD_HAPPY == MOOD_HAPPY && PET_PACK_MOOD_SAD == MOOD_SAD &&
                   PET_PACK_MOOD_EXCITED == MOOD_EXCITED && PET_PACK_MOOD_ANXIOUS == MOOD_ANXIOUS &&
                   PET_PACK_MOOD_SLEEPING == MOOD_SLEEPING && PET_PACK_MOOD_ANGRY == MOOD_ANGRY &&
                   PET_PACK_MOOD_PROUD == MOOD_PROUD && PET_PACK_MOOD_IDLE == MOOD_IDLE &&
                   PET_PACK_MOOD_FOCUSED == MOOD_FOCUSED && PET_PACK_MOOD_WORKING == MOOD_WORKING &&
                   PET_PACK_MOOD_THINKING == MOOD_THINKING && PET_PACK_MOODS == MOOD_COUNT,
               "proto_mood_t no longer matches the format-1 mood numbers");
_Static_assert(PET_PACK_ACC_NONE == PET_ACC_NONE && PET_PACK_ACC_ZZZ == PET_ACC_ZZZ &&
                   PET_PACK_ACC_SWEAT == PET_ACC_SWEAT && PET_PACK_ACC_TEAR == PET_ACC_TEAR &&
                   PET_PACK_ACC_SPARKLES == PET_ACC_SPARKLES && PET_PACK_ACC_DOTS == PET_ACC_DOTS &&
                   PET_PACK_ACC_TONGUE == PET_ACC_TONGUE && PET_PACK_ACC_COUNT == PET_ACC_COUNT,
               "pet_accessory_t no longer matches the format-1 accessory numbers");
_Static_assert(PET_PACK_CH_COUNT == PET_POSE_CHANNELS, "pet_pose channel table and format 1 disagree");
_Static_assert(PET_PACK_HEADER_SIZE + PET_PACK_PALETTE_SIZE + PET_PACK_FACE_SIZE + PET_PACK_AUX_SIZE == 164,
               "fixed part of the pack moved");
_Static_assert(PET_PACK_AUX_SIZE == PET_PACK_AUX_ROWS * PET_PACK_AUX_COUNT, "aux block size");

#define OFF_PALETTE  PET_PACK_HEADER_SIZE
#define OFF_FACE     (OFF_PALETTE + PET_PACK_PALETTE_SIZE)
#define OFF_AUX      (OFF_FACE + PET_PACK_FACE_SIZE)
#define OFF_VAR      (OFF_AUX + PET_PACK_AUX_SIZE)
/* The fixed blocks, one rrect and the CRC. */
#define SMALLEST     (OFF_VAR + 2 + 5 + PET_PACK_CRC_SIZE)

#define KIND_SHAPE(k)    ((k) & 0x03)
#define KIND_LAYER(k)    (((k) >> 2) & 0x03)
#define KIND_DRIVERS(k)  (((k) >> 4) & 0x07)
#define KIND_MIRROR      0x80
#define ROLE_COLOUR(r)   ((r) & 0x0F)
#define ROLE_NO_OUTLINE  0x10
#define ROLE_ANCHOR      0x20
#define ROLE_RING        0x40
#define ROLE_SQUASH      0x80
#define LINE_COUNT(b)    ((b) & 0x07)
#define LINE_WIDTH(b)    ((b) >> 3)
#define DRV_AUX(b)       ((b) & 0x03)
#define DRV_OP(b)        (((b) >> 2) & 0x03)
#define DRV_ARG(b)       (((b) >> 4) & 0x07)
#define OSC_AUX(b)       ((b) & 0x03)
#define OSC_WAVE(b)      (((b) >> 2) & 0x03)
#define OSC_RESERVED     0xF0
#define OSC_UNIT_MS      50

typedef struct {
    int16_t min, max;
} range_t;

/* Values a face override may set; anything else would only be clamped later, so it is refused. */
static const range_t CHANNEL_RANGE[PET_PACK_CH_COUNT] = {
    [PET_PACK_CH_EYE_OPEN] = {0, 100},       [PET_PACK_CH_EYE_SIZE] = {4, 48},
    [PET_PACK_CH_PUPIL_SIZE] = {0, 100},     [PET_PACK_CH_PUPIL_DX] = {-100, 100},
    [PET_PACK_CH_PUPIL_DY] = {-100, 100},    [PET_PACK_CH_BROW_ANGLE] = {-60, 60},
    [PET_PACK_CH_MOUTH_CURVE] = {-100, 100}, [PET_PACK_CH_MOUTH_OPEN] = {0, 100},
    [PET_PACK_CH_BOB_AMP_PX] = {0, 12},      [PET_PACK_CH_BOB_PERIOD_MS] = {0, 20000},
    [PET_PACK_CH_BLINK_MIN_MS] = {0, 30000}, [PET_PACK_CH_BLINK_MAX_MS] = {0, 30000},
    [PET_PACK_CH_JITTER_PX] = {0, 6},        [PET_PACK_CH_ACCESSORY] = {0, PET_PACK_ACC_COUNT - 1},
    [PET_PACK_CH_GLANCE] = {0, 2},           [PET_PACK_CH_LID] = {-60, 60},
    [PET_PACK_CH_BROW_L] = {-60, 60},        [PET_PACK_CH_BROW_R] = {-60, 60},
    [PET_PACK_CH_BROW_LIFT] = {-20, 20},     [PET_PACK_CH_MOUTH_DX] = {-30, 30},
    [PET_PACK_CH_MOUTH_W] = {0, 60},
};

static const char *const ERR_NAMES[] = {
    [PET_PACK_OK] = "ok",
    [PET_PACK_ERR_LENGTH] = "length",
    [PET_PACK_ERR_CRC] = "crc",
    [PET_PACK_ERR_MAGIC] = "magic",
    [PET_PACK_ERR_FMT] = "fmt",
    [PET_PACK_ERR_ID] = "id",
    [PET_PACK_ERR_NAME] = "name",
    [PET_PACK_ERR_COUNT] = "count",
    [PET_PACK_ERR_RESERVED] = "reserved",
    [PET_PACK_ERR_RANGE] = "range",
    [PET_PACK_ERR_PART] = "part",
    [PET_PACK_ERR_ANCHOR] = "anchor",
    [PET_PACK_ERR_DRIVER] = "driver",
    [PET_PACK_ERR_TRUNCATED] = "truncated",
    [PET_PACK_ERR_TRAILING] = "trailing",
};

uint32_t pet_pack_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rgb(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

bool pet_pack_id_ok(const char *id)
{
    size_t n = 0;
    for (; id[n] != '\0'; n++) {
        const char c = id[n];
        if (n >= PET_PACK_ID_LEN || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return n > 0;
}

/* A NUL-padded text field: at least one accepted byte, then only NULs. */
static bool text_field(const uint8_t *p, size_t cap, bool id, char *out)
{
    size_t n = 0;
    while (n < cap && p[n] != 0) {
        const uint8_t c = p[n];
        const bool ok = id ? ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) : (c >= 0x20 && c <= 0x7E);
        if (!ok) {
            return false;
        }
        n++;
    }
    for (size_t i = n; i < cap; i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    if (out != NULL) {
        memcpy(out, p, n);
        out[n] = '\0';
    }
    return n > 0;
}

int pet_pack_row(uint8_t mood)
{
    if (mood < PET_PACK_MOODS) {
        return mood;
    }
    if (mood >= PET_PACK_REACT_PAT && mood <= PET_PACK_REACT_ATTENTION) {
        return PET_PACK_MOODS + (mood - PET_PACK_REACT_PAT);
    }
    return -1;
}

/* Reads the variable part (overrides, oscillators, parts) from p..end; out may be NULL. */
typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} cursor_t;

static bool take(cursor_t *c, size_t n, const uint8_t **at)
{
    if ((size_t)(c->end - c->p) < n) {
        return false;
    }
    *at = c->p;
    c->p += n;
    return true;
}

static pet_pack_err_t parse_part(cursor_t *c, int index, const uint8_t *kinds, pet_part_t *out)
{
    const uint8_t *b;
    if (!take(c, 2, &b)) {
        return PET_PACK_ERR_TRUNCATED;
    }
    const uint8_t kind = b[0];
    const uint8_t role = b[1];
    const uint8_t shape = KIND_SHAPE(kind);
    const uint8_t n_drv = KIND_DRIVERS(kind);
    if (shape > PET_SHAPE_TRI || n_drv > PET_PACK_MAX_DRIVERS) {
        return PET_PACK_ERR_PART;
    }
    if (ROLE_COLOUR(role) >= PET_PACK_ROLE_COUNT) {
        return PET_PACK_ERR_RANGE;
    }
    if ((role & (ROLE_RING | ROLE_SQUASH)) != 0 && shape != PET_SHAPE_RRECT) {
        return PET_PACK_ERR_RESERVED;
    }
    pet_part_t part = {
        .shape = shape,
        .layer = KIND_LAYER(kind),
        .role = ROLE_COLOUR(role),
        .mirrored = (kind & KIND_MIRROR) != 0,
        .no_outline = (role & ROLE_NO_OUTLINE) != 0,
        .ring = (role & ROLE_RING) != 0,
        .squash = (role & ROLE_SQUASH) != 0,
        .anchor = -1,
        .n_drv = n_drv,
    };
    if (role & ROLE_ANCHOR) {
        const uint8_t *a;
        if (!take(c, 1, &a)) {
            return PET_PACK_ERR_TRUNCATED;
        }
        if (a[0] >= index || KIND_SHAPE(kinds[a[0]]) != PET_SHAPE_LINE) {
            return PET_PACK_ERR_ANCHOR;
        }
        part.anchor = (int8_t)a[0];
    }
    if (shape == PET_SHAPE_RRECT) {
        if (!take(c, 5, &b)) {
            return PET_PACK_ERR_TRUNCATED;
        }
        part.n_pts = 1;
        part.pts[0] = (pet_pt_t){(int8_t)b[0], (int8_t)b[1]};
        part.w = b[2];
        part.h = b[3];
        part.r = b[4];
        const uint8_t short_side = part.w < part.h ? part.w : part.h;
        if (part.w == 0 || part.h == 0 || part.r > short_side / 2) {
            return PET_PACK_ERR_PART;
        }
    } else {
        uint8_t n = 3;
        if (shape == PET_SHAPE_LINE) {
            if (!take(c, 1, &b)) {
                return PET_PACK_ERR_TRUNCATED;
            }
            n = LINE_COUNT(b[0]);
            part.width = LINE_WIDTH(b[0]);
            if (n < 2 || n > PET_PACK_MAX_POINTS || part.width == 0) {
                return PET_PACK_ERR_PART;
            }
        }
        if (!take(c, (size_t)n * 2, &b)) {
            return PET_PACK_ERR_TRUNCATED;
        }
        part.n_pts = n;
        for (uint8_t i = 0; i < n; i++) {
            part.pts[i] = (pet_pt_t){(int8_t)b[2 * i], (int8_t)b[2 * i + 1]};
        }
    }
    for (uint8_t i = 0; i < n_drv; i++) {
        if (!take(c, 2, &b)) {
            return PET_PACK_ERR_TRUNCATED;
        }
        const pet_driver_t d = {.aux = DRV_AUX(b[0]), .op = DRV_OP(b[0]), .arg = DRV_ARG(b[0]), .scale = (int8_t)b[1]};
        if (d.op == PET_DRV_MOVE_X || d.op == PET_DRV_MOVE_Y) {
            const bool whole = d.arg == PET_DRV_WHOLE;
            if (!whole && (shape == PET_SHAPE_RRECT || d.arg >= part.n_pts)) {
                return PET_PACK_ERR_DRIVER;
            }
        } else if (d.scale != 0 || (d.op == PET_DRV_SHOW_NONZERO && d.arg != 0)) {
            return PET_PACK_ERR_RESERVED;
        }
        part.drv[i] = d;
    }
    if (out != NULL) {
        *out = part;
    }
    return PET_PACK_OK;
}

static pet_pack_err_t parse(const uint8_t *bytes, size_t len, pet_def_t *out)
{
    if (bytes == NULL || len < SMALLEST || len > PET_PACK_MAX_SIZE) {
        return PET_PACK_ERR_LENGTH;
    }
    const size_t body = len - PET_PACK_CRC_SIZE;
    if (pet_pack_crc32(bytes, body) != rd32(bytes + body)) {
        return PET_PACK_ERR_CRC;
    }
    if (memcmp(bytes, "DPET", 4) != 0) {
        return PET_PACK_ERR_MAGIC;
    }
    if (bytes[4] != PET_PACK_FMT) {
        return PET_PACK_ERR_FMT;
    }
    const uint8_t rev = bytes[5];
    if (rev == 0) {
        return PET_PACK_ERR_RANGE;
    }
    if (rd16(bytes + 6) != len) {
        return PET_PACK_ERR_LENGTH;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    char id[PET_PACK_ID_LEN + 1];
    char name[PET_PACK_NAME_LEN + 1];
    if (!text_field(bytes + 8, PET_PACK_ID_LEN, true, id)) {
        return PET_PACK_ERR_ID;
    }
    if (!text_field(bytes + 16, PET_PACK_NAME_LEN, false, name)) {
        return PET_PACK_ERR_NAME;
    }
    const uint8_t flags = bytes[28];
    const uint8_t n_parts = bytes[29];
    const uint8_t n_over = bytes[30];
    const uint8_t n_osc = bytes[31];
    const uint8_t modes = PET_PACK_FLAG_SHIFT | PET_PACK_FLAG_GLOW;
    if ((flags & ~modes) != 0 || (flags & modes) == modes) {
        return PET_PACK_ERR_RESERVED;
    }
    if (n_parts == 0 || n_parts > PET_PACK_MAX_PARTS || n_over > PET_PACK_MAX_OVER || n_osc > PET_PACK_MAX_OSC) {
        return PET_PACK_ERR_COUNT;
    }
    const bool shift = (flags & PET_PACK_FLAG_SHIFT) != 0;
    const uint8_t *pal = bytes + OFF_PALETTE;
    for (int m = 0; m < PET_PACK_MOODS; m++) {
        if (!shift && pal[12 + 4 * m + 3] != 0) {
            return PET_PACK_ERR_RESERVED;
        }
    }
    const uint8_t *f = bytes + OFF_FACE;
    const pet_face_t face = {
        .eye_style = f[0],
        .mouth_set = f[1],
        .brows = f[2] != 0,
        .eye_dx = f[3],
        .eye_y = (int8_t)f[4],
        .mouth_y = (int8_t)f[5],
        .mouth_hw = f[6],
        .sleep_drop = f[7],
        .acc_mask = f[8],
        .eye_sq = f[9],
        .mouth_sq = f[10],
        .sweat_y = (int8_t)f[11],
    };
    if (face.eye_style > PET_EYE_RING || face.mouth_set > PET_MOUTH_SNOUT || f[2] > 1 || face.eye_dx < 4 ||
        face.eye_dx > 60 || face.mouth_hw < 4 || face.mouth_hw > 60 || face.sleep_drop > 30 || face.eye_sq > 16 ||
        face.mouth_sq > 16) {
        return PET_PACK_ERR_RANGE;
    }
    if ((face.acc_mask & ~(((1u << PET_PACK_ACC_COUNT) - 1) & ~1u)) != 0) {
        return PET_PACK_ERR_RESERVED;
    }

    cursor_t c = {.p = bytes + OFF_VAR, .end = bytes + body};
    const uint8_t *b;
    if (!take(&c, (size_t)n_over * 4, &b)) {
        return PET_PACK_ERR_TRUNCATED;
    }
    pet_override_t over[PET_PACK_MAX_OVER];
    for (uint8_t i = 0; i < n_over; i++) {
        const uint8_t *e = b + 4 * i;
        over[i] = (pet_override_t){.mood = e[0], .channel = e[1], .value = (int16_t)rd16(e + 2)};
        if (pet_pack_row(over[i].mood) < 0 || over[i].channel >= PET_PACK_CH_COUNT) {
            return PET_PACK_ERR_RANGE;
        }
        const range_t r = CHANNEL_RANGE[over[i].channel];
        const bool lid_off = over[i].channel == PET_PACK_CH_LID && over[i].value == PET_PACK_LID_OFF;
        if (!lid_off && (over[i].value < r.min || over[i].value > r.max)) {
            return PET_PACK_ERR_RANGE;
        }
    }
    if (!take(&c, (size_t)n_osc * 4, &b)) {
        return PET_PACK_ERR_TRUNCATED;
    }
    pet_osc_t osc[PET_PACK_MAX_OSC];
    for (uint8_t i = 0; i < n_osc; i++) {
        const uint8_t *e = b + 4 * i;
        if (pet_pack_row(e[0]) < 0) {
            return PET_PACK_ERR_RANGE;
        }
        if ((e[1] & OSC_RESERVED) != 0) {
            return PET_PACK_ERR_RESERVED;
        }
        osc[i] = (pet_osc_t){.mood = e[0], .aux = OSC_AUX(e[1]), .wave = OSC_WAVE(e[1]), .amp = (int8_t)e[2],
                             .period_ms = (uint16_t)(e[3] * OSC_UNIT_MS)};
        if ((osc[i].wave == PET_WAVE_SINE || osc[i].wave == PET_WAVE_JITTER) && e[3] == 0) {
            return PET_PACK_ERR_RANGE;
        }
    }
    uint8_t kinds[PET_PACK_MAX_PARTS];
    for (uint8_t i = 0; i < n_parts; i++) {
        if (c.p >= c.end) {
            return PET_PACK_ERR_TRUNCATED;
        }
        kinds[i] = c.p[0];
        const pet_pack_err_t e = parse_part(&c, i, kinds, out != NULL ? &out->parts[i] : NULL);
        if (e != PET_PACK_OK) {
            if (out != NULL) {
                memset(out, 0, sizeof(*out));
            }
            return e;
        }
    }
    if (c.p != c.end) {
        if (out != NULL) {
            memset(out, 0, sizeof(*out));
        }
        return PET_PACK_ERR_TRAILING;
    }
    if (out == NULL) {
        return PET_PACK_OK;
    }
    out->rev = rev;
    out->size = (uint16_t)len;
    out->crc32 = rd32(bytes + body);
    memcpy(out->id, id, sizeof(id));
    memcpy(out->name, name, sizeof(name));
    out->shift_palette = shift;
    out->glow_palette = (flags & PET_PACK_FLAG_GLOW) != 0;
    out->base = rgb(pal);
    out->accent = rgb(pal + 3);
    out->cream = rgb(pal + 6);
    out->sclera = rgb(pal + 9);
    for (int m = 0; m < PET_PACK_MOODS; m++) {
        out->mood_rgb[m] = rgb(pal + 12 + 4 * m);
        out->mood_t[m] = pal[12 + 4 * m + 3];
    }
    out->face = face;
    for (int r = 0; r < PET_PACK_AUX_ROWS; r++) {
        for (int a = 0; a < PET_PACK_AUX_COUNT; a++) {
            out->aux[r][a] = (int8_t)bytes[OFF_AUX + r * PET_PACK_AUX_COUNT + a];
        }
    }
    out->n_over = n_over;
    memcpy(out->over, over, sizeof(over[0]) * n_over);
    out->n_osc = n_osc;
    memcpy(out->osc, osc, sizeof(osc[0]) * n_osc);
    out->n_parts = n_parts;
    return PET_PACK_OK;
}

pet_pack_err_t pet_pack_validate(const uint8_t *bytes, size_t len)
{
    return parse(bytes, len, NULL);
}

pet_pack_err_t pet_pack_decode(const uint8_t *bytes, size_t len, pet_def_t *out)
{
    const pet_pack_err_t e = parse(bytes, len, out);
    if (e != PET_PACK_OK) {
        memset(out, 0, sizeof(*out));
    }
    return e;
}

const char *pet_pack_err_name(pet_pack_err_t e)
{
    return (unsigned)e < sizeof(ERR_NAMES) / sizeof(ERR_NAMES[0]) ? ERR_NAMES[e] : "?";
}

const char *pet_pack_err_wire(pet_pack_err_t e)
{
    return e == PET_PACK_ERR_CRC ? "crc" : "format";
}

uint32_t pet_pack_mix(uint32_t a, uint32_t b, unsigned t)
{
    if (t > 255) {
        t = 255;
    }
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const uint32_t ca = (a >> shift) & 0xFF;
        const uint32_t cb = (b >> shift) & 0xFF;
        out |= ((ca * (255 - t) + cb * t + 127) / 255) << shift;
    }
    return out;
}

uint32_t pet_pack_mood_color(const pet_def_t *def, unsigned mood)
{
    if (mood >= PET_PACK_MOODS) {
        mood = PET_PACK_MOOD_IDLE;
    }
    return def->shift_palette ? pet_pack_mix(def->base, def->mood_rgb[mood], def->mood_t[mood])
                              : def->mood_rgb[mood];
}

uint32_t pet_pack_body_color(const pet_def_t *def, uint32_t mood_color)
{
    return def->glow_palette ? def->base : mood_color;
}
