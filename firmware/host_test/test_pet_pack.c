#include "pet_builtin.h"
#include "pet_pack.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static uint8_t s_buf[PET_PACK_MAX_SIZE + 64];
static pet_def_t s_def;

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Rewrites size and CRC after a test changed the bytes, so the check under test is reached. */
static void reseal(uint8_t *p, size_t len)
{
    put16(p + 6, (uint16_t)len);
    const uint32_t crc = pet_pack_crc32(p, len - 4);
    p[len - 4] = (uint8_t)crc;
    p[len - 3] = (uint8_t)(crc >> 8);
    p[len - 2] = (uint8_t)(crc >> 16);
    p[len - 1] = (uint8_t)(crc >> 24);
}

/* A minimal pack: fixed blocks, the given variable bytes, CRC. Returns its length. */
static size_t build(uint8_t *out, const uint8_t *var, size_t var_len, uint8_t n_parts, uint8_t n_over, uint8_t n_osc)
{
    memset(out, 0, 164);
    memcpy(out, "DPET", 4);
    out[4] = PET_PACK_FMT;
    out[5] = 1;
    memcpy(out + 8, "test", 4);
    memcpy(out + 16, "Test", 4);
    out[29] = n_parts;
    out[30] = n_over;
    out[31] = n_osc;
    uint8_t *face = out + 88;
    face[3] = 27;  /* eye_dx */
    face[6] = 18;  /* mouth_hw */
    face[9] = 16;
    face[10] = 16;
    memcpy(out + 164, var, var_len);
    const size_t len = 164 + var_len + 4;
    reseal(out, len);
    return len;
}

/* One rrect: kind 0 (rrect, behind, no drivers), role body, centre 0,0, 20x10 r5. */
static const uint8_t RRECT[] = {0x00, 0x00, 0, 0, 20, 10, 5};

static pet_pack_err_t build_check(const uint8_t *var, size_t len, uint8_t parts, uint8_t over, uint8_t osc)
{
    return pet_pack_validate(s_buf, build(s_buf, var, len, parts, over, osc));
}

static size_t read_file(const char *name, uint8_t *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", PETS_GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    const size_t n = fread(out, 1, cap, f);
    fclose(f);
    return n;
}

static void test_builtins_equal_the_golden_files(void)
{
    for (unsigned i = 0; i < PET_BUILTIN_COUNT; i++) {
        const pet_builtin_t *b = pet_builtin_at(i);
        char name[32];
        snprintf(name, sizeof(name), "%s.pet", b->id);
        const size_t n = read_file(name, s_buf, sizeof(s_buf));
        TEST_ASSERT_EQUAL_size_t(n, b->len);
        TEST_ASSERT_EQUAL_MEMORY(s_buf, b->bytes, n);
        snprintf(name, sizeof(name), "%s.crc32", b->id);
        char text[16] = {0};
        read_file(name, (uint8_t *)text, sizeof(text) - 1);
        char want[9];
        snprintf(want, sizeof(want), "%08lx", (unsigned long)pet_pack_crc32(b->bytes, b->len - 4));
        TEST_ASSERT_EQUAL_MEMORY(want, text, 8);
    }
    TEST_ASSERT_NULL(pet_builtin_at(PET_BUILTIN_COUNT));
    TEST_ASSERT_NULL(pet_builtin_find("nope"));
    TEST_ASSERT_EQUAL_STRING(PET_BUILTIN_DEFAULT, pet_builtin_at(0)->id);
}

/* The generated table is pets/builtin.txt: its ids in its order, comments and blank lines skipped. */
static void test_builtins_follow_the_list(void)
{
    char text[1024] = {0};
    read_file("../builtin.txt", (uint8_t *)text, sizeof(text) - 1);
    unsigned n = 0;
    for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char id[16] = {0};
        if (sscanf(line, " %15s", id) != 1) {
            continue;
        }
        TEST_ASSERT_TRUE_MESSAGE(n < PET_BUILTIN_COUNT, id);
        TEST_ASSERT_EQUAL_STRING(id, pet_builtin_at(n)->id);
        TEST_ASSERT_EQUAL_PTR(pet_builtin_at(n), pet_builtin_find(id));
        n++;
    }
    TEST_ASSERT_EQUAL_UINT(PET_BUILTIN_COUNT, n);
}

static void test_golden_packs_decode(void)
{
    const pet_builtin_t *devo = pet_builtin_find("devo");
    TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_decode(devo->bytes, devo->len, &s_def));
    TEST_ASSERT_EQUAL_STRING("devo", s_def.id);
    TEST_ASSERT_EQUAL_STRING("Devo", s_def.name);
    TEST_ASSERT_EQUAL(1, s_def.rev);
    TEST_ASSERT_FALSE(s_def.shift_palette);
    TEST_ASSERT_EQUAL_HEX32(0xF0C594, pet_pack_mood_color(&s_def, PET_PACK_MOOD_IDLE));
    TEST_ASSERT_EQUAL_HEX32(0x5B82F0, pet_pack_mood_color(&s_def, PET_PACK_MOOD_FOCUSED));
    TEST_ASSERT_EQUAL(PET_EYE_ROUND, s_def.face.eye_style);
    TEST_ASSERT_TRUE(s_def.face.brows);
    TEST_ASSERT_EQUAL(-8, s_def.face.eye_y);
    TEST_ASSERT_EQUAL(12, s_def.n_parts);
    TEST_ASSERT_TRUE(s_def.parts[9].squash);
    TEST_ASSERT_EQUAL(140, s_def.parts[9].w);
    TEST_ASSERT_TRUE(s_def.parts[1].mirrored);
    TEST_ASSERT_EQUAL(30, s_def.aux[PET_PACK_MOOD_SAD][0]);
    TEST_ASSERT_EQUAL(2, s_def.aux[PET_PACK_MOODS + 4][3]);
    TEST_ASSERT_EQUAL(500, s_def.osc[5].period_ms);

    const pet_builtin_t *ember = pet_builtin_find("ember");
    TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_decode(ember->bytes, ember->len, &s_def));
    TEST_ASSERT_EQUAL_STRING("Ember", s_def.name);
    TEST_ASSERT_TRUE(s_def.shift_palette);
    TEST_ASSERT_EQUAL_HEX32(0x2FAE9C, pet_pack_mood_color(&s_def, PET_PACK_MOOD_IDLE));
    TEST_ASSERT_EQUAL_HEX32(pet_pack_mix(0x2FAE9C, 0xFFFFFF, 31), pet_pack_mood_color(&s_def, PET_PACK_MOOD_HAPPY));
    TEST_ASSERT_EQUAL(PET_EYE_SLIT, s_def.face.eye_style);
    TEST_ASSERT_EQUAL(PET_MOUTH_SNOUT, s_def.face.mouth_set);
    TEST_ASSERT_EQUAL(27, s_def.n_parts);
    TEST_ASSERT_EQUAL(1, s_def.parts[2].anchor);
    TEST_ASSERT_EQUAL(PET_SHAPE_TRI, s_def.parts[2].shape);
    TEST_ASSERT_TRUE(s_def.n_over > 20);
}

static void test_every_truncation_fails(void)
{
    for (unsigned i = 0; i < PET_BUILTIN_COUNT; i++) {
        const pet_builtin_t *b = pet_builtin_at(i);
        for (size_t len = 0; len < b->len; len++) {
            /* A heap copy of exactly len bytes so ASan sees any read past the end. */
            uint8_t *copy = malloc(len ? len : 1);
            memcpy(copy, b->bytes, len);
            TEST_ASSERT_NOT_EQUAL(PET_PACK_OK, pet_pack_validate(copy, len));
            /* The same with a matching size and CRC: the structure checks must catch it. */
            if (len >= 168) {
                reseal(copy, len);
                TEST_ASSERT_NOT_EQUAL(PET_PACK_OK, pet_pack_validate(copy, len));
            }
            free(copy);
        }
        TEST_ASSERT_EQUAL(PET_PACK_ERR_LENGTH, pet_pack_validate(NULL, 0));
    }
}

static void test_every_bit_flip_is_a_crc_error(void)
{
    const pet_builtin_t *b = pet_builtin_find("ember");
    const size_t n = b->len;
    memcpy(s_buf, b->bytes, n);
    for (size_t i = 0; i < n; i++) {
        for (int bit = 0; bit < 8; bit++) {
            s_buf[i] ^= (uint8_t)(1u << bit);
            const pet_pack_err_t e = pet_pack_validate(s_buf, n);
            TEST_ASSERT_EQUAL(PET_PACK_ERR_CRC, e);
            TEST_ASSERT_EQUAL_STRING("crc", pet_pack_err_wire(e));
            s_buf[i] ^= (uint8_t)(1u << bit);
        }
    }
    TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_validate(s_buf, n));
}

static uint32_t s_rng = 0x12345678;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* Random edits behind a valid CRC: the decoder must refuse or decode, never read out of bounds. */
static void test_resealed_fuzz(void)
{
    for (int round = 0; round < 20000; round++) {
        const pet_builtin_t *b = pet_builtin_at((unsigned)round % PET_BUILTIN_COUNT);
        size_t n = b->len;
        memcpy(s_buf, b->bytes, n);
        const int edits = 1 + (int)(rnd() % 4);
        for (int e = 0; e < edits; e++) {
            const size_t at = 8 + rnd() % (n - 12);
            s_buf[at] = (uint8_t)rnd();
        }
        if (rnd() % 4 == 0) {
            n = 168 + rnd() % (n - 168);
        }
        uint8_t *copy = malloc(n);
        memcpy(copy, s_buf, n);
        reseal(copy, n);
        if (pet_pack_decode(copy, n, &s_def) == PET_PACK_OK) {
            TEST_ASSERT_TRUE(s_def.n_parts >= 1 && s_def.n_parts <= PET_PACK_MAX_PARTS);
            TEST_ASSERT_TRUE(pet_pack_id_ok(s_def.id));
            for (int p = 0; p < s_def.n_parts; p++) {
                TEST_ASSERT_TRUE(s_def.parts[p].anchor < p);
                TEST_ASSERT_TRUE(s_def.parts[p].n_pts >= 1 && s_def.parts[p].n_pts <= PET_PACK_MAX_POINTS);
            }
        }
        free(copy);
    }
}

static void test_size_and_format(void)
{
    const pet_builtin_t *b = pet_builtin_find("devo");
    const size_t n = b->len;
    memcpy(s_buf, b->bytes, n);
    s_buf[4] = 2;
    reseal(s_buf, n);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_FMT, pet_pack_validate(s_buf, n));
    TEST_ASSERT_EQUAL_STRING("format", pet_pack_err_wire(PET_PACK_ERR_FMT));
    memcpy(s_buf, b->bytes, n);
    s_buf[0] = 'X';
    reseal(s_buf, n);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_MAGIC, pet_pack_validate(s_buf, n));
    memcpy(s_buf, b->bytes, n);
    s_buf[5] = 0;
    reseal(s_buf, n);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_RANGE, pet_pack_validate(s_buf, n));
    /* The header's size must be the length. */
    memcpy(s_buf, b->bytes, n);
    reseal(s_buf, n);
    put16(s_buf + 6, (uint16_t)(n + 1));
    reseal(s_buf, n);
    TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_validate(s_buf, n));
    put16(s_buf + 6, (uint16_t)(n + 1));
    const uint32_t crc = pet_pack_crc32(s_buf, n - 4);
    memcpy(s_buf + n - 4, &crc, 4);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_LENGTH, pet_pack_validate(s_buf, n));
    /* Over 2048 bytes is refused before anything is read. */
    memset(s_buf, 0, sizeof(s_buf));
    TEST_ASSERT_EQUAL(PET_PACK_ERR_LENGTH, pet_pack_validate(s_buf, PET_PACK_MAX_SIZE + 1));
    /* Trailing bytes after the last part. */
    const uint8_t extra[] = {0x00, 0x00, 0, 0, 20, 10, 5, 0xAA};
    TEST_ASSERT_EQUAL(PET_PACK_ERR_TRAILING, build_check(extra, sizeof(extra), 1, 0, 0));
    TEST_ASSERT_EQUAL(PET_PACK_OK, build_check(RRECT, sizeof(RRECT), 1, 0, 0));
}

static void test_header_fields(void)
{
    build(s_buf, RRECT, sizeof(RRECT), 1, 0, 0);
    memcpy(s_buf + 8, "Devo", 4);
    reseal(s_buf, 164 + sizeof(RRECT) + 4);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_ID, pet_pack_validate(s_buf, 164 + sizeof(RRECT) + 4));
    build(s_buf, RRECT, sizeof(RRECT), 1, 0, 0);
    s_buf[8 + 6] = 'x'; /* a byte after the NUL padding */
    reseal(s_buf, 164 + sizeof(RRECT) + 4);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_ID, pet_pack_validate(s_buf, 164 + sizeof(RRECT) + 4));
    build(s_buf, RRECT, sizeof(RRECT), 1, 0, 0);
    s_buf[17] = 0x07;
    reseal(s_buf, 164 + sizeof(RRECT) + 4);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_NAME, pet_pack_validate(s_buf, 164 + sizeof(RRECT) + 4));
    build(s_buf, RRECT, sizeof(RRECT), 1, 0, 0);
    s_buf[28] = 0x02;
    reseal(s_buf, 164 + sizeof(RRECT) + 4);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_RESERVED, pet_pack_validate(s_buf, 164 + sizeof(RRECT) + 4));
    /* Table palette: the fourth byte of a mood entry is reserved. */
    build(s_buf, RRECT, sizeof(RRECT), 1, 0, 0);
    s_buf[32 + 12 + 3] = 1;
    reseal(s_buf, 164 + sizeof(RRECT) + 4);
    TEST_ASSERT_EQUAL(PET_PACK_ERR_RESERVED, pet_pack_validate(s_buf, 164 + sizeof(RRECT) + 4));
    /* acc_mask bit 0 (none) and bit 7 are reserved; face numbers stay in their tables. */
    const size_t n = 164 + sizeof(RRECT) + 4;
    const struct {
        int at;
        uint8_t v;
        pet_pack_err_t want;
    } FACE[] = {
        {88 + 8, 0x01, PET_PACK_ERR_RESERVED}, {88 + 8, 0x80, PET_PACK_ERR_RESERVED},
        {88 + 0, 3, PET_PACK_ERR_RANGE},       {88 + 1, 2, PET_PACK_ERR_RANGE},
        {88 + 2, 2, PET_PACK_ERR_RANGE},       {88 + 3, 0, PET_PACK_ERR_RANGE},
        {88 + 9, 17, PET_PACK_ERR_RANGE},      {88 + 8, 0x7E, PET_PACK_OK},
    };
    for (size_t i = 0; i < sizeof(FACE) / sizeof(FACE[0]); i++) {
        build(s_buf, RRECT, sizeof(RRECT), 1, 0, 0);
        s_buf[FACE[i].at] = FACE[i].v;
        reseal(s_buf, n);
        TEST_ASSERT_EQUAL(FACE[i].want, pet_pack_validate(s_buf, n));
    }
}

static void test_counts(void)
{
    TEST_ASSERT_EQUAL(PET_PACK_ERR_COUNT, build_check(RRECT, sizeof(RRECT), 0, 0, 0));
    TEST_ASSERT_EQUAL(PET_PACK_ERR_COUNT, build_check(RRECT, sizeof(RRECT), PET_PACK_MAX_PARTS + 1, 0, 0));
    TEST_ASSERT_EQUAL(PET_PACK_ERR_COUNT, build_check(RRECT, sizeof(RRECT), 1, PET_PACK_MAX_OVER + 1, 0));
    TEST_ASSERT_EQUAL(PET_PACK_ERR_COUNT, build_check(RRECT, sizeof(RRECT), 1, 0, PET_PACK_MAX_OSC + 1));
    /* More parts announced than present. */
    TEST_ASSERT_EQUAL(PET_PACK_ERR_TRUNCATED, build_check(RRECT, sizeof(RRECT), 2, 0, 0));
    /* An override announced but only the part present: its 4 bytes eat the part. */
    TEST_ASSERT_NOT_EQUAL(PET_PACK_OK, build_check(RRECT, sizeof(RRECT), 1, 1, 0));
}

static void test_overrides_and_oscillators(void)
{
    uint8_t v[32];
    const struct {
        uint8_t e[4];
        pet_pack_err_t want;
    } OVER[] = {
        {{PET_PACK_MOOD_IDLE, PET_PACK_CH_MOUTH_CURVE, 30, 0}, PET_PACK_OK},
        {{PET_PACK_REACT_ATTENTION, PET_PACK_CH_LID, 0xE8, 0x03}, PET_PACK_OK}, /* 1000: no lid */
        {{11, PET_PACK_CH_MOUTH_CURVE, 0, 0}, PET_PACK_ERR_RANGE},
        {{21, PET_PACK_CH_MOUTH_CURVE, 0, 0}, PET_PACK_ERR_RANGE},
        {{PET_PACK_MOOD_IDLE, PET_PACK_CH_COUNT, 0, 0}, PET_PACK_ERR_RANGE},
        {{PET_PACK_MOOD_IDLE, PET_PACK_CH_ACCESSORY, PET_PACK_ACC_COUNT, 0}, PET_PACK_ERR_RANGE},
        {{PET_PACK_MOOD_IDLE, PET_PACK_CH_GLANCE, 3, 0}, PET_PACK_ERR_RANGE},
        {{PET_PACK_MOOD_IDLE, PET_PACK_CH_EYE_OPEN, 101, 0}, PET_PACK_ERR_RANGE},
        {{PET_PACK_MOOD_IDLE, PET_PACK_CH_BOB_PERIOD_MS, 0xFF, 0xFF}, PET_PACK_ERR_RANGE},
    };
    for (size_t i = 0; i < sizeof(OVER) / sizeof(OVER[0]); i++) {
        memcpy(v, OVER[i].e, 4);
        memcpy(v + 4, RRECT, sizeof(RRECT));
        TEST_ASSERT_EQUAL_MESSAGE(OVER[i].want, build_check(v, 4 + sizeof(RRECT), 1, 1, 0), "override");
    }
    const struct {
        uint8_t e[4];
        pet_pack_err_t want;
    } OSC[] = {
        {{PET_PACK_MOOD_IDLE, 0x00, 3, 70}, PET_PACK_OK},
        {{PET_PACK_REACT_PAT, 0x04 | 1, 3, 0}, PET_PACK_OK}, /* bob: no period */
        {{PET_PACK_MOOD_IDLE, 0x10, 3, 70}, PET_PACK_ERR_RESERVED},
        {{PET_PACK_MOOD_IDLE, 0x00, 3, 0}, PET_PACK_ERR_RANGE},
        {{15, 0x00, 3, 1}, PET_PACK_ERR_RANGE},
    };
    for (size_t i = 0; i < sizeof(OSC) / sizeof(OSC[0]); i++) {
        memcpy(v, OSC[i].e, 4);
        memcpy(v + 4, RRECT, sizeof(RRECT));
        TEST_ASSERT_EQUAL_MESSAGE(OSC[i].want, build_check(v, 4 + sizeof(RRECT), 1, 0, 1), "oscillator");
    }
}

static void test_parts(void)
{
    const struct {
        uint8_t bytes[24];
        size_t len;
        uint8_t parts;
        pet_pack_err_t want;
    } CASES[] = {
        /* line, 2 points, width 4, then a tri anchored to it */
        {{0x01, 0x01, 2 | (4 << 3), 0, 0, 10, 10, 0x02, 0x20, 0, 0, 0, 5, 0, 0, 5}, 16, 2, PET_PACK_OK},
        /* anchor pointing forward (to itself) */
        {{0x01, 0x01, 2 | (4 << 3), 0, 0, 10, 10, 0x02, 0x20, 1, 0, 0, 5, 0, 0, 5}, 16, 2, PET_PACK_ERR_ANCHOR},
        /* anchor to a rrect */
        {{0x00, 0x00, 0, 0, 20, 10, 5, 0x02, 0x20, 0, 0, 0, 5, 0, 0, 5}, 16, 2, PET_PACK_ERR_ANCHOR},
        /* shape 3 */
        {{0x03, 0x00, 0, 0, 20, 10, 5}, 7, 1, PET_PACK_ERR_PART},
        /* five drivers */
        {{0x50, 0x00, 0, 0, 20, 10, 5}, 7, 1, PET_PACK_ERR_PART},
        /* colour role 13 */
        {{0x00, 13, 0, 0, 20, 10, 5}, 7, 1, PET_PACK_ERR_RANGE},
        /* radius over half the short side, zero width */
        {{0x00, 0x00, 0, 0, 20, 10, 6}, 7, 1, PET_PACK_ERR_PART},
        {{0x00, 0x00, 0, 0, 0, 10, 0}, 7, 1, PET_PACK_ERR_PART},
        /* ring and squash only on a rrect */
        {{0x01, 0x40, 2 | (4 << 3), 0, 0, 10, 10}, 7, 1, PET_PACK_ERR_RESERVED},
        {{0x00, 0xC0, 0, 0, 20, 10, 5}, 7, 1, PET_PACK_OK},
        /* line with 1 and 5 points, width 0 */
        {{0x01, 0x00, 1 | (4 << 3), 0, 0, 0, 0}, 7, 1, PET_PACK_ERR_PART},
        {{0x01, 0x00, 5 | (4 << 3), 0, 0, 0, 0}, 7, 1, PET_PACK_ERR_PART},
        {{0x01, 0x00, 2, 0, 0, 1, 1}, 7, 1, PET_PACK_ERR_PART},
        /* tri with a move driver on point 2 (ok) and point 3 (out of range) */
        {{0x12, 0x00, 0, 0, 0, 5, 5, 0, 0x01 | (2 << 4), 16}, 10, 1, PET_PACK_OK},
        {{0x12, 0x00, 0, 0, 0, 5, 5, 0, 0x01 | (3 << 4), 16}, 10, 1, PET_PACK_ERR_DRIVER},
        /* rrect move driver must move the whole part */
        {{0x10, 0x00, 0, 0, 20, 10, 5, 0x04 | (7 << 4), 16}, 9, 1, PET_PACK_OK},
        {{0x10, 0x00, 0, 0, 20, 10, 5, 0x04 | (0 << 4), 16}, 9, 1, PET_PACK_ERR_DRIVER},
        /* show drivers carry no scale; show-nonzero no k */
        {{0x10, 0x00, 0, 0, 20, 10, 5, 0x0C | (3 << 4), 0}, 9, 1, PET_PACK_OK},
        {{0x10, 0x00, 0, 0, 20, 10, 5, 0x0C | (3 << 4), 1}, 9, 1, PET_PACK_ERR_RESERVED},
        {{0x10, 0x00, 0, 0, 20, 10, 5, 0x08 | (1 << 4), 0}, 9, 1, PET_PACK_ERR_RESERVED},
        /* a driver cut off by the end */
        {{0x10, 0x00, 0, 0, 20, 10, 5, 0x08}, 8, 1, PET_PACK_ERR_TRUNCATED},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "part case %u", (unsigned)i);
        TEST_ASSERT_EQUAL_MESSAGE(CASES[i].want, build_check(CASES[i].bytes, CASES[i].len, CASES[i].parts, 0, 0), msg);
    }
}

static void test_names_and_ids(void)
{
    TEST_ASSERT_TRUE(pet_pack_id_ok("mochi"));
    TEST_ASSERT_TRUE(pet_pack_id_ok("a1234567"));
    TEST_ASSERT_FALSE(pet_pack_id_ok("a12345678"));
    TEST_ASSERT_FALSE(pet_pack_id_ok(""));
    TEST_ASSERT_FALSE(pet_pack_id_ok("Mochi"));
    TEST_ASSERT_FALSE(pet_pack_id_ok("../x"));
    TEST_ASSERT_EQUAL_STRING("anchor", pet_pack_err_name(PET_PACK_ERR_ANCHOR));
    TEST_ASSERT_EQUAL_STRING("?", pet_pack_err_name((pet_pack_err_t)99));
    TEST_ASSERT_EQUAL(-1, pet_pack_row(11));
    TEST_ASSERT_EQUAL(15, pet_pack_row(PET_PACK_REACT_ATTENTION));
    /* The CRC is zlib's. */
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, pet_pack_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0x808080, pet_pack_mix(0x000000, 0xFFFFFF, 128));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_builtins_equal_the_golden_files);
    RUN_TEST(test_builtins_follow_the_list);
    RUN_TEST(test_golden_packs_decode);
    RUN_TEST(test_every_truncation_fails);
    RUN_TEST(test_every_bit_flip_is_a_crc_error);
    RUN_TEST(test_resealed_fuzz);
    RUN_TEST(test_size_and_format);
    RUN_TEST(test_header_fields);
    RUN_TEST(test_counts);
    RUN_TEST(test_overrides_and_oscillators);
    RUN_TEST(test_parts);
    RUN_TEST(test_names_and_ids);
    return UNITY_END();
}
