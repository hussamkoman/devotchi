#include "pet_builtin.h"
#include "pet_install.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- a fake store that can fail or lose power at any mutating step ---- */

#define FAKE_KEYS  12

typedef struct {
    char id[PET_PACK_ID_LEN + 1];
    uint8_t bytes[PET_PACK_MAX_SIZE];
    size_t len;
} fake_key_t;

typedef struct {
    fake_key_t keys[FAKE_KEYS];
    char selected[PET_PACK_ID_LEN + 1];
    /* Mutating step (1-based) at which the power goes; 0 = never. */
    int crash_at;
    /* The crashing step still lands (NVS committed it) or not. */
    bool crash_lands;
    int steps;
    bool dead;
    bool fail_put;
    bool fail_select;
    bool corrupt_reads;
} fake_t;

static fake_t s_fake;

static fake_key_t *find(fake_t *f, const char *id)
{
    for (int i = 0; i < FAKE_KEYS; i++) {
        if (f->keys[i].len > 0 && strcmp(f->keys[i].id, id) == 0) {
            return &f->keys[i];
        }
    }
    return NULL;
}

/* True when the step may change state. */
static bool step(fake_t *f)
{
    if (f->dead) {
        return false;
    }
    f->steps++;
    if (f->crash_at != 0 && f->steps == f->crash_at) {
        f->dead = true;
        return f->crash_lands;
    }
    return true;
}

static bool f_put(void *ctx, const char *id, const uint8_t *bytes, size_t len)
{
    fake_t *f = ctx;
    if (f->fail_put || !step(f)) {
        return false;
    }
    fake_key_t *k = find(f, id);
    for (int i = 0; k == NULL && i < FAKE_KEYS; i++) {
        if (f->keys[i].len == 0) {
            k = &f->keys[i];
        }
    }
    snprintf(k->id, sizeof(k->id), "%s", id);
    memcpy(k->bytes, bytes, len);
    k->len = len;
    return !f->dead;
}

static size_t f_get(void *ctx, const char *id, uint8_t *buf, size_t cap)
{
    fake_t *f = ctx;
    const fake_key_t *k = find(f, id);
    if (f->dead || k == NULL || k->len > cap) {
        return 0;
    }
    memcpy(buf, k->bytes, k->len);
    if (f->corrupt_reads) {
        buf[k->len / 2] ^= 0x40;
    }
    return k->len;
}

static bool f_remove(void *ctx, const char *id)
{
    fake_t *f = ctx;
    if (!step(f)) {
        return false;
    }
    fake_key_t *k = find(f, id);
    if (k != NULL) {
        k->len = 0;
    }
    return !f->dead;
}

static bool f_has(void *ctx, const char *id)
{
    return find(ctx, id) != NULL;
}

static size_t f_count(void *ctx)
{
    fake_t *f = ctx;
    size_t n = 0;
    for (int i = 0; i < FAKE_KEYS; i++) {
        n += f->keys[i].len > 0;
    }
    return n;
}

static bool f_select(void *ctx, const char *id)
{
    fake_t *f = ctx;
    if (f->fail_select || !step(f)) {
        return false;
    }
    snprintf(f->selected, sizeof(f->selected), "%s", id);
    return !f->dead;
}

static const pet_store_ops_t OPS = {
    .ctx = &s_fake, .put = f_put, .get = f_get, .remove = f_remove, .has = f_has, .count = f_count,
    .select = f_select, .slots = PET_STORE_SLOTS,
};

static void fake_reset(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
    snprintf(s_fake.selected, sizeof(s_fake.selected), "devo");
}

/* After a "reboot": power back, nothing failing. */
static void fake_reboot(void)
{
    s_fake.dead = false;
    s_fake.crash_at = 0;
    s_fake.fail_put = s_fake.fail_select = s_fake.corrupt_reads = false;
}

/* ---- packs ---- */

static uint8_t s_pack[PET_PACK_MAX_SIZE];
static size_t s_pack_len;
static uint8_t s_scratch[PET_PACK_MAX_SIZE];

static void reseal(uint8_t *p, size_t len)
{
    const uint32_t crc = pet_pack_crc32(p, len - 4);
    for (int i = 0; i < 4; i++) {
        p[len - 4 + i] = (uint8_t)(crc >> (8 * i));
    }
}

/* Devo's pack renamed to id with revision rev. */
static size_t make_pack(uint8_t *out, const char *id, uint8_t rev)
{
    const pet_builtin_t *b = pet_builtin_find("devo");
    memcpy(out, b->bytes, b->len);
    memset(out + 8, 0, PET_PACK_ID_LEN);
    memcpy(out + 8, id, strlen(id));
    out[5] = rev;
    reseal(out, b->len);
    return b->len;
}

static uint32_t crc_of(const uint8_t *p, size_t len)
{
    return pet_pack_crc32(p, len - 4);
}

static pet_cmd_t install_cmd(const char *id, uint8_t rev, const uint8_t *pack, size_t len, bool select)
{
    pet_cmd_t c = {.op = PROTO_PET_OP_INSTALL, .rev = rev, .size = (uint16_t)len, .crc32 = crc_of(pack, len),
                   .select = select};
    snprintf(c.id, sizeof(c.id), "%s", id);
    return c;
}

/* ---- pet_cmd_check ---- */

static proto_pet_cmd_t raw(proto_pet_op_t op, const char *pet, int32_t rev, int32_t size, const char *crc)
{
    proto_pet_cmd_t r = {.op = op, .rev = rev, .size = size};
    if (pet != NULL) {
        r.pet_present = true;
        r.pet_long = strlen(pet) >= sizeof(r.pet);
        if (!r.pet_long) {
            snprintf(r.pet, sizeof(r.pet), "%s", pet);
        }
    }
    if (crc != NULL) {
        snprintf(r.crc32, sizeof(r.crc32), "%s", crc);
    }
    return r;
}

static void test_cmd_check(void)
{
    pet_cmd_t c;
    proto_pet_cmd_t r = raw(PROTO_PET_OP_INSTALL, "mochi", 1, 612, "1a2b3c4d");
    r.select = true;
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_cmd_check(&r, &c));
    TEST_ASSERT_EQUAL_STRING("mochi", c.id);
    TEST_ASSERT_EQUAL(1, c.rev);
    TEST_ASSERT_EQUAL(612, c.size);
    TEST_ASSERT_EQUAL_HEX32(0x1a2b3c4d, c.crc32);
    TEST_ASSERT_TRUE(c.select);

    const char *const BAD_IDS[] = {"../x", "Mochi", "abcdefghi", "", "mo chi", "mochi\n", "a/b", "%2e%2e"};
    for (size_t i = 0; i < sizeof(BAD_IDS) / sizeof(BAD_IDS[0]); i++) {
        r = raw(PROTO_PET_OP_INSTALL, BAD_IDS[i], 1, 612, "1a2b3c4d");
        TEST_ASSERT_EQUAL_MESSAGE(PET_RESULT_UNKNOWN, pet_cmd_check(&r, &c), BAD_IDS[i]);
        TEST_ASSERT_EQUAL_STRING("", c.id);
    }
    r = raw(PROTO_PET_OP_INSTALL, "a-very-long-pet-name", 1, 612, "1a2b3c4d");
    TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_cmd_check(&r, &c));
    r = raw(PROTO_PET_OP_INSTALL, NULL, 1, 612, "1a2b3c4d");
    TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_cmd_check(&r, &c));

    const struct {
        int32_t rev, size;
        const char *crc;
    } BAD[] = {
        {0, 612, "1a2b3c4d"},   {256, 612, "1a2b3c4d"}, {-1, 612, "1a2b3c4d"}, {1, 35, "1a2b3c4d"},
        {1, 2049, "1a2b3c4d"},  {1, -1, "1a2b3c4d"},    {1, 612, "1A2B3C4D"},  {1, 612, "1a2b3c4"},
        {1, 612, "1a2b3c4d5"},  {1, 612, "1a2b3c4g"},   {1, 612, NULL},
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        r = raw(PROTO_PET_OP_INSTALL, "mochi", BAD[i].rev, BAD[i].size, BAD[i].crc);
        TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_cmd_check(&r, &c));
    }
    r = raw(PROTO_PET_OP_INSTALL, "mochi", 255, 2048, "00000000");
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_cmd_check(&r, &c));
    r = raw(PROTO_PET_OP_INSTALL, "devo", 1, 612, "1a2b3c4d");
    TEST_ASSERT_EQUAL(PET_RESULT_BUILTIN, pet_cmd_check(&r, &c));
    r = raw(PROTO_PET_OP_REMOVE, "ember", -1, -1, NULL);
    TEST_ASSERT_EQUAL(PET_RESULT_BUILTIN, pet_cmd_check(&r, &c));
    r = raw(PROTO_PET_OP_SELECT, "ember", -1, -1, NULL);
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_cmd_check(&r, &c));
    r = raw(PROTO_PET_OP_REMOVE, "mochi", -1, -1, NULL);
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_cmd_check(&r, &c));
    r = raw(PROTO_PET_OP_UNKNOWN, "mochi", 1, 612, "1a2b3c4d");
    TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_cmd_check(&r, &c));
    TEST_ASSERT_EQUAL_STRING("mismatch", pet_result_name(PET_RESULT_MISMATCH));
    TEST_ASSERT_EQUAL_STRING("builtin", pet_result_name(PET_RESULT_BUILTIN));
}

/* proto_parse keeps a malformed pet_cmd so it can be answered. */
static void test_proto_pet_cmd(void)
{
    proto_msg_t m;
    const char *ok = "{\"type\":\"pet_cmd\",\"op\":\"install\",\"pet\":\"mochi\",\"rev\":1,\"size\":612,"
                     "\"crc32\":\"1a2b3c4d\",\"select\":true}";
    TEST_ASSERT_TRUE(proto_parse(ok, strlen(ok), &m));
    TEST_ASSERT_EQUAL(PROTO_PET_CMD, m.type);
    TEST_ASSERT_EQUAL(PROTO_PET_OP_INSTALL, m.u.pet_cmd.op);
    TEST_ASSERT_EQUAL_STRING("mochi", m.u.pet_cmd.pet);
    TEST_ASSERT_TRUE(m.u.pet_cmd.select);
    const char *bad = "{\"type\":\"pet_cmd\",\"op\":\"zap\",\"pet\":7,\"rev\":1.5,\"size\":\"x\"}";
    TEST_ASSERT_TRUE(proto_parse(bad, strlen(bad), &m));
    TEST_ASSERT_EQUAL(PROTO_PET_OP_UNKNOWN, m.u.pet_cmd.op);
    TEST_ASSERT_FALSE(m.u.pet_cmd.pet_present);
    TEST_ASSERT_EQUAL(-1, m.u.pet_cmd.rev);
    TEST_ASSERT_EQUAL(-1, m.u.pet_cmd.size);
    const char *longer = "{\"type\":\"pet_cmd\",\"op\":\"select\",\"pet\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}";
    TEST_ASSERT_TRUE(proto_parse(longer, strlen(longer), &m));
    TEST_ASSERT_TRUE(m.u.pet_cmd.pet_long);
    pet_cmd_t c;
    TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_cmd_check(&m.u.pet_cmd, &c));

    char buf[PROTO_PETS_MAX_BYTES + 1];
    proto_pets_t p = {.selected = "devo", .slots = 8, .free = 7, .n_installed = 3,
                      .installed = {{"devo", 1, true}, {"ember", 1, true}, {"mochi", 2, false}},
                      .result_op = "install", .result_pet = "mochi", .result_ok = false, .result_error = "crc"};
    TEST_ASSERT_TRUE(proto_write_pets(buf, sizeof(buf), &p) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"pets\",\"selected\":\"devo\",\"slots\":8,\"free\":7,\"installed\":["
                             "{\"id\":\"devo\",\"rev\":1,\"builtin\":true},{\"id\":\"ember\",\"rev\":1,\"builtin\":true},"
                             "{\"id\":\"mochi\",\"rev\":2}],\"result\":{\"op\":\"install\",\"pet\":\"mochi\","
                             "\"ok\":false,\"error\":\"crc\"}}",
                             buf);
    /* The largest message (10 pets, longest ids, a result) stays under 700 bytes. */
    p.n_installed = PET_BUILTIN_COUNT + PET_STORE_SLOTS;
    for (size_t i = 0; i < p.n_installed; i++) {
        p.installed[i] = (proto_pet_entry_t){"abcdefgh", 255, i < 2};
    }
    p.selected = "abcdefgh";
    p.result_pet = "abcdefgh";
    p.result_error = "mismatch";
    TEST_ASSERT_TRUE(proto_write_pets(buf, sizeof(buf), &p) > 0);
}

/* ---- download checks ---- */

static void test_download_check_order(void)
{
    s_pack_len = make_pack(s_pack, "mochi", 1);
    pet_cmd_t c = install_cmd("mochi", 1, s_pack, s_pack_len, false);
    pet_download_t dl = {.status = 200, .content_length = (int32_t)s_pack_len, .body = s_pack,
                         .received = s_pack_len, .complete = true};
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_download_check(&c, &dl));

    pet_download_t d = dl;
    d.status = 404;
    TEST_ASSERT_EQUAL(PET_RESULT_HTTP, pet_download_check(&c, &d));
    d = dl;
    d.content_length = (int32_t)s_pack_len + 1;
    TEST_ASSERT_EQUAL(PET_RESULT_SIZE, pet_download_check(&c, &d));
    d.content_length = -1;
    TEST_ASSERT_EQUAL(PET_RESULT_SIZE, pet_download_check(&c, &d));
    d = dl;
    d.received = s_pack_len - 10;
    TEST_ASSERT_EQUAL(PET_RESULT_HTTP, pet_download_check(&c, &d));
    d = dl;
    d.complete = false;
    TEST_ASSERT_EQUAL(PET_RESULT_HTTP, pet_download_check(&c, &d));

    /* A CRC that differs from the command, then a body whose trailer is wrong. */
    pet_cmd_t c2 = c;
    c2.crc32 ^= 1;
    TEST_ASSERT_EQUAL(PET_RESULT_CRC, pet_download_check(&c2, &dl));
    s_pack[s_pack_len - 1] ^= 1;
    TEST_ASSERT_EQUAL(PET_RESULT_CRC, pet_download_check(&c, &dl));
    s_pack[s_pack_len - 1] ^= 1;

    /* fmt 2 with a correct CRC is a format error. */
    s_pack[4] = 2;
    reseal(s_pack, s_pack_len);
    c2 = install_cmd("mochi", 1, s_pack, s_pack_len, false);
    TEST_ASSERT_EQUAL(PET_RESULT_FORMAT, pet_download_check(&c2, &dl));
    s_pack[4] = 1;
    reseal(s_pack, s_pack_len);

    /* Header id or rev not the command's. */
    c2 = install_cmd("other", 1, s_pack, s_pack_len, false);
    TEST_ASSERT_EQUAL(PET_RESULT_MISMATCH, pet_download_check(&c2, &dl));
    c2 = install_cmd("mochi", 2, s_pack, s_pack_len, false);
    TEST_ASSERT_EQUAL(PET_RESULT_MISMATCH, pet_download_check(&c2, &dl));
}

/* ---- install, select, remove ---- */

static void test_install_and_select(void)
{
    fake_reset();
    s_pack_len = make_pack(s_pack, "mochi", 1);
    pet_cmd_t c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
    bool sel;
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    TEST_ASSERT_TRUE(sel);
    TEST_ASSERT_EQUAL_STRING("mochi", s_fake.selected);
    const uint8_t *bytes;
    size_t len;
    TEST_ASSERT_EQUAL(PET_SOURCE_STORED, pet_install_resolve(&OPS, "mochi", s_scratch, sizeof(s_scratch), &bytes, &len));
    TEST_ASSERT_EQUAL_MEMORY(s_pack, bytes, len);
    TEST_ASSERT_EQUAL(PET_SOURCE_BUILTIN, pet_install_resolve(&OPS, "ember", s_scratch, sizeof(s_scratch), &bytes, &len));
    TEST_ASSERT_EQUAL(PET_SOURCE_FALLBACK, pet_install_resolve(&OPS, "nope", s_scratch, sizeof(s_scratch), &bytes, &len));
    TEST_ASSERT_EQUAL_MEMORY(pet_builtin_find(PET_BUILTIN_DEFAULT)->bytes, bytes, len);
    TEST_ASSERT_EQUAL(PET_SOURCE_FALLBACK, pet_install_resolve(&OPS, "../x", s_scratch, sizeof(s_scratch), &bytes, &len));

    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_select(&OPS, "ember", s_scratch, sizeof(s_scratch)));
    TEST_ASSERT_EQUAL_STRING("ember", s_fake.selected);
    TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_install_select(&OPS, "nope", s_scratch, sizeof(s_scratch)));
    TEST_ASSERT_EQUAL_STRING("ember", s_fake.selected);
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_select(&OPS, "mochi", s_scratch, sizeof(s_scratch)));

    bool re;
    TEST_ASSERT_EQUAL(PET_RESULT_BUILTIN, pet_install_remove(&OPS, "devo", "mochi", &re));
    TEST_ASSERT_EQUAL(PET_RESULT_UNKNOWN, pet_install_remove(&OPS, "nope", "mochi", &re));
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_remove(&OPS, "mochi", "mochi", &re));
    TEST_ASSERT_TRUE(re);
    TEST_ASSERT_EQUAL_STRING("devo", s_fake.selected);
    TEST_ASSERT_FALSE(f_has(&s_fake, "mochi"));
}

static void test_full_and_failures(void)
{
    fake_reset();
    bool sel;
    for (int i = 0; i < PET_STORE_SLOTS; i++) {
        char id[8];
        snprintf(id, sizeof(id), "p%d", i);
        s_pack_len = make_pack(s_pack, id, 1);
        pet_cmd_t c = install_cmd(id, 1, s_pack, s_pack_len, false);
        TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
        TEST_ASSERT_FALSE(sel);
    }
    s_pack_len = make_pack(s_pack, "mochi", 1);
    pet_cmd_t c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
    TEST_ASSERT_EQUAL(PET_RESULT_FULL, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    TEST_ASSERT_EQUAL_STRING("devo", s_fake.selected);
    /* Replacing a stored id needs no free slot. */
    s_pack_len = make_pack(s_pack, "p3", 2);
    c = install_cmd("p3", 2, s_pack, s_pack_len, false);
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));

    /* A fresh install whose read-back fails is removed again. */
    fake_reset();
    s_fake.corrupt_reads = true;
    s_pack_len = make_pack(s_pack, "mochi", 1);
    c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
    TEST_ASSERT_EQUAL(PET_RESULT_STORAGE, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    TEST_ASSERT_FALSE(f_has(&s_fake, "mochi"));
    TEST_ASSERT_EQUAL_STRING("devo", s_fake.selected);

    /* A replace whose read-back fails reports storage and is not removed. */
    fake_reset();
    s_pack_len = make_pack(s_pack, "mochi", 1);
    c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
    TEST_ASSERT_EQUAL(PET_RESULT_OK, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    s_fake.corrupt_reads = true;
    s_pack_len = make_pack(s_pack, "mochi", 2);
    c = install_cmd("mochi", 2, s_pack, s_pack_len, false);
    TEST_ASSERT_EQUAL(PET_RESULT_STORAGE, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    TEST_ASSERT_TRUE(f_has(&s_fake, "mochi"));

    /* A failing put keeps the old value (NVS replaces atomically). */
    fake_reset();
    s_fake.fail_put = true;
    TEST_ASSERT_EQUAL(PET_RESULT_STORAGE, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    TEST_ASSERT_EQUAL(0, f_count(&s_fake));

    /* A failing select leaves the pack installed and the selection as it was. */
    fake_reset();
    s_fake.fail_select = true;
    c.select = true;
    TEST_ASSERT_EQUAL(PET_RESULT_STORAGE, pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    TEST_ASSERT_FALSE(sel);
    TEST_ASSERT_EQUAL_STRING("devo", s_fake.selected);

    /* No `pets` partition: nothing can be stored. */
    fake_reset();
    pet_store_ops_t none = OPS;
    none.slots = 0;
    TEST_ASSERT_EQUAL(PET_RESULT_STORAGE, pet_install_commit(&none, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
}

/* After any reboot: s_pet names a pet that loads, and the stored mochi is rev 1 or rev 2, never broken. */
static void check_after_reboot(uint8_t old_rev, uint8_t new_rev, const char *old_sel)
{
    const uint8_t *bytes;
    size_t len;
    const pet_source_t src = pet_install_resolve(&OPS, s_fake.selected, s_scratch, sizeof(s_scratch), &bytes, &len);
    TEST_ASSERT_TRUE(src != PET_SOURCE_FALLBACK);
    TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_validate(bytes, len));
    TEST_ASSERT_TRUE(strcmp(s_fake.selected, old_sel) == 0 || strcmp(s_fake.selected, "mochi") == 0);
    const fake_key_t *k = find(&s_fake, "mochi");
    if (k != NULL) {
        TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_validate(k->bytes, k->len));
        TEST_ASSERT_TRUE(k->bytes[5] == old_rev || k->bytes[5] == new_rev);
    }
    if (strcmp(s_fake.selected, "mochi") == 0) {
        TEST_ASSERT_NOT_NULL(k);
        TEST_ASSERT_EQUAL(new_rev, k->bytes[5]);
    }
}

static void test_power_loss_at_every_step(void)
{
    for (int lands = 0; lands < 2; lands++) {
        for (int at = 1; at <= 4; at++) {
            /* Fresh install with select. */
            fake_reset();
            s_fake.crash_at = at;
            s_fake.crash_lands = lands;
            s_pack_len = make_pack(s_pack, "mochi", 1);
            pet_cmd_t c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
            bool sel;
            pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel);
            fake_reboot();
            check_after_reboot(1, 1, "devo");

            /* Replace rev 1 (selected) by rev 2. */
            fake_reset();
            c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
            pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel);
            s_fake.steps = 0;
            s_fake.crash_at = at;
            s_fake.crash_lands = lands;
            s_pack_len = make_pack(s_pack, "mochi", 2);
            c = install_cmd("mochi", 2, s_pack, s_pack_len, true);
            pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel);
            fake_reboot();
            TEST_ASSERT_EQUAL_STRING("mochi", s_fake.selected);
            const fake_key_t *k = find(&s_fake, "mochi");
            TEST_ASSERT_NOT_NULL(k);
            TEST_ASSERT_TRUE(k->bytes[5] == 1 || k->bytes[5] == 2);
            TEST_ASSERT_EQUAL(PET_PACK_OK, pet_pack_validate(k->bytes, k->len));

            /* Remove of the selected pet. */
            fake_reset();
            s_pack_len = make_pack(s_pack, "mochi", 1);
            c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
            pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel);
            s_fake.steps = 0;
            s_fake.crash_at = at;
            s_fake.crash_lands = lands;
            bool re;
            pet_install_remove(&OPS, "mochi", "mochi", &re);
            fake_reboot();
            check_after_reboot(1, 1, "devo");
        }
    }
}

/* Power lost between the blob write and s_pet: the new pack is installed, the old selection stays. */
static void test_power_loss_between_blob_and_selection(void)
{
    fake_reset();
    snprintf(s_fake.selected, sizeof(s_fake.selected), "ember");
    s_fake.crash_at = 2; /* step 1 is the put, step 2 the select */
    s_fake.crash_lands = false;
    s_pack_len = make_pack(s_pack, "mochi", 1);
    pet_cmd_t c = install_cmd("mochi", 1, s_pack, s_pack_len, true);
    bool sel;
    TEST_ASSERT_EQUAL(PET_RESULT_STORAGE,
                      pet_install_commit(&OPS, &c, s_pack, s_pack_len, s_scratch, sizeof(s_scratch), &sel));
    fake_reboot();
    TEST_ASSERT_TRUE(f_has(&s_fake, "mochi"));
    TEST_ASSERT_EQUAL_STRING("ember", s_fake.selected);
}

/* ---- partition init wrapper ---- */

typedef struct {
    int init_rc[3];
    int inits;
    int erases;
    char erased[16];
} part_fake_t;

static int p_init(void *ctx, const char *label)
{
    part_fake_t *p = ctx;
    TEST_ASSERT_EQUAL_STRING(PET_STORE_LABEL, label);
    return p->init_rc[p->inits++];
}

static bool p_erase(void *ctx, const char *label)
{
    part_fake_t *p = ctx;
    TEST_ASSERT_TRUE(pet_part_erase_allowed(label));
    p->erases++;
    snprintf(p->erased, sizeof(p->erased), "%s", label);
    return true;
}

static void test_partition_init(void)
{
    part_fake_t p = {.init_rc = {0}};
    pet_part_ops_t ops = {.ctx = &p, .init = p_init, .erase = p_erase};
    TEST_ASSERT_EQUAL(PET_PART_OK, pet_part_init(&ops));
    TEST_ASSERT_EQUAL(0, p.erases);

    p = (part_fake_t){.init_rc = {1}};
    TEST_ASSERT_EQUAL(PET_PART_ABSENT, pet_part_init(&ops));
    TEST_ASSERT_EQUAL(0, p.erases);

    /* Random bytes: init fails, the partition is erased by label once, init works. */
    p = (part_fake_t){.init_rc = {-1, 0}};
    TEST_ASSERT_EQUAL(PET_PART_ERASED, pet_part_init(&ops));
    TEST_ASSERT_EQUAL(1, p.erases);
    TEST_ASSERT_EQUAL_STRING("pets", p.erased);
    TEST_ASSERT_EQUAL(2, p.inits);

    p = (part_fake_t){.init_rc = {-1, -1}};
    TEST_ASSERT_EQUAL(PET_PART_FAILED, pet_part_init(&ops));
    TEST_ASSERT_EQUAL(1, p.erases);

    TEST_ASSERT_FALSE(pet_part_erase_allowed("nvs"));
    TEST_ASSERT_FALSE(pet_part_erase_allowed("phy_init"));
    TEST_ASSERT_FALSE(pet_part_erase_allowed(NULL));
    TEST_ASSERT_FALSE(pet_part_erase_allowed("pets2"));
    TEST_ASSERT_EQUAL_STRING("ok", pet_part_state_name(PET_PART_OK));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cmd_check);
    RUN_TEST(test_proto_pet_cmd);
    RUN_TEST(test_download_check_order);
    RUN_TEST(test_install_and_select);
    RUN_TEST(test_full_and_failures);
    RUN_TEST(test_power_loss_at_every_step);
    RUN_TEST(test_power_loss_between_blob_and_selection);
    RUN_TEST(test_partition_init);
    return UNITY_END();
}
