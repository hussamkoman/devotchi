#include "pet_install.h"

#include "pet_builtin.h"

#include <string.h>

static const char *const RESULT_NAMES[] = {
    [PET_RESULT_OK] = "ok",           [PET_RESULT_HTTP] = "http",         [PET_RESULT_SIZE] = "size",
    [PET_RESULT_CRC] = "crc",         [PET_RESULT_FORMAT] = "format",     [PET_RESULT_MISMATCH] = "mismatch",
    [PET_RESULT_FULL] = "full",       [PET_RESULT_UNKNOWN] = "unknown",   [PET_RESULT_BUILTIN] = "builtin",
    [PET_RESULT_BUSY] = "busy",       [PET_RESULT_STORAGE] = "storage",
};

static const char *const PART_NAMES[] = {
    [PET_PART_OK] = "ok",
    [PET_PART_ERASED] = "erased",
    [PET_PART_ABSENT] = "absent",
    [PET_PART_FAILED] = "error",
};

const char *pet_result_name(pet_result_t r)
{
    return (size_t)r < sizeof(RESULT_NAMES) / sizeof(RESULT_NAMES[0]) ? RESULT_NAMES[r] : "unknown";
}

const char *pet_part_state_name(pet_part_state_t s)
{
    return (size_t)s < sizeof(PART_NAMES) / sizeof(PART_NAMES[0]) ? PART_NAMES[s] : "error";
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

pet_result_t pet_cmd_check(const proto_pet_cmd_t *in, pet_cmd_t *out)
{
    memset(out, 0, sizeof(*out));
    if (in->op == PROTO_PET_OP_UNKNOWN || !in->pet_present || in->pet_long) {
        return PET_RESULT_UNKNOWN;
    }
    /* Byte by byte into a fresh buffer: nothing unchecked reaches the id (and later the URL). */
    size_t n = 0;
    for (; in->pet[n] != '\0'; n++) {
        const char c = in->pet[n];
        if (n >= PET_PACK_ID_LEN || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            memset(out, 0, sizeof(*out));
            return PET_RESULT_UNKNOWN;
        }
        out->id[n] = c;
    }
    if (n == 0) {
        return PET_RESULT_UNKNOWN;
    }
    out->id[n] = '\0';
    out->op = in->op;
    out->select = in->select;
    if (in->op == PROTO_PET_OP_INSTALL) {
        if (in->rev < 1 || in->rev > 255 || in->size < PET_PACK_MIN_SIZE || in->size > PET_PACK_MAX_SIZE ||
            in->crc_long || strlen(in->crc32) != 8) {
            memset(out, 0, sizeof(*out));
            return PET_RESULT_UNKNOWN;
        }
        uint32_t crc = 0;
        for (int i = 0; i < 8; i++) {
            const int d = hex_digit(in->crc32[i]);
            if (d < 0) {
                memset(out, 0, sizeof(*out));
                return PET_RESULT_UNKNOWN;
            }
            crc = (crc << 4) | (uint32_t)d;
        }
        out->rev = (uint8_t)in->rev;
        out->size = (uint16_t)in->size;
        out->crc32 = crc;
    }
    if (in->op != PROTO_PET_OP_SELECT && pet_builtin_find(out->id) != NULL) {
        return PET_RESULT_BUILTIN;
    }
    return PET_RESULT_OK;
}

static uint32_t trailer(const uint8_t *p, size_t len)
{
    const uint8_t *t = p + len - PET_PACK_CRC_SIZE;
    return (uint32_t)t[0] | ((uint32_t)t[1] << 8) | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);
}

pet_result_t pet_download_check(const pet_cmd_t *cmd, const pet_download_t *dl)
{
    if (dl->status != 200) {
        return PET_RESULT_HTTP;
    }
    if (dl->content_length != (int32_t)cmd->size) {
        return PET_RESULT_SIZE;
    }
    if (!dl->complete || dl->received != cmd->size || dl->body == NULL) {
        return PET_RESULT_HTTP;
    }
    const size_t len = dl->received;
    if (len < PET_PACK_CRC_SIZE) {
        return PET_RESULT_FORMAT;
    }
    const uint32_t crc = pet_pack_crc32(dl->body, len - PET_PACK_CRC_SIZE);
    if (crc != cmd->crc32 || crc != trailer(dl->body, len)) {
        return PET_RESULT_CRC;
    }
    const pet_pack_err_t e = pet_pack_validate(dl->body, len);
    if (e != PET_PACK_OK) {
        return e == PET_PACK_ERR_CRC ? PET_RESULT_CRC : PET_RESULT_FORMAT;
    }
    /* id at 8..15 (NUL padded, already validated), rev at 5. */
    char id[PET_PACK_ID_LEN + 1] = {0};
    memcpy(id, dl->body + 8, PET_PACK_ID_LEN);
    if (strcmp(id, cmd->id) != 0 || dl->body[5] != cmd->rev) {
        return PET_RESULT_MISMATCH;
    }
    return PET_RESULT_OK;
}

static bool stored_valid(const pet_store_ops_t *ops, const char *id, uint8_t *buf, size_t cap, size_t *len)
{
    const size_t n = ops->get(ops->ctx, id, buf, cap);
    if (n == 0 || pet_pack_validate(buf, n) != PET_PACK_OK) {
        return false;
    }
    char stored_id[PET_PACK_ID_LEN + 1] = {0};
    memcpy(stored_id, buf + 8, PET_PACK_ID_LEN);
    if (strcmp(stored_id, id) != 0) {
        return false;
    }
    if (len != NULL) {
        *len = n;
    }
    return true;
}

pet_result_t pet_install_commit(const pet_store_ops_t *ops, const pet_cmd_t *cmd, const uint8_t *pack, size_t len,
                                uint8_t *scratch, size_t scratch_cap, bool *selected)
{
    *selected = false;
    if (ops->slots == 0) {
        return PET_RESULT_STORAGE;
    }
    const bool existed = ops->has(ops->ctx, cmd->id);
    if (!existed && ops->count(ops->ctx) >= ops->slots) {
        return PET_RESULT_FULL;
    }
    if (!ops->put(ops->ctx, cmd->id, pack, len)) {
        return PET_RESULT_STORAGE;
    }
    size_t back = 0;
    if (!stored_valid(ops, cmd->id, scratch, scratch_cap, &back) || back != len || memcmp(scratch, pack, len) != 0) {
        if (!existed) {
            ops->remove(ops->ctx, cmd->id);
        }
        return PET_RESULT_STORAGE;
    }
    if (cmd->select) {
        if (!ops->select(ops->ctx, cmd->id)) {
            return PET_RESULT_STORAGE;
        }
        *selected = true;
    }
    return PET_RESULT_OK;
}

pet_result_t pet_install_select(const pet_store_ops_t *ops, const char *id, uint8_t *scratch, size_t scratch_cap)
{
    if (pet_builtin_find(id) == NULL && !stored_valid(ops, id, scratch, scratch_cap, NULL)) {
        return PET_RESULT_UNKNOWN;
    }
    return ops->select(ops->ctx, id) ? PET_RESULT_OK : PET_RESULT_STORAGE;
}

pet_result_t pet_install_remove(const pet_store_ops_t *ops, const char *id, const char *selected, bool *reselected)
{
    *reselected = false;
    if (pet_builtin_find(id) != NULL) {
        return PET_RESULT_BUILTIN;
    }
    if (!ops->has(ops->ctx, id)) {
        return PET_RESULT_UNKNOWN;
    }
    /* The selection moves first: a reset in between leaves Devo selected and the pack still stored. */
    if (selected != NULL && strcmp(selected, id) == 0) {
        if (!ops->select(ops->ctx, PET_BUILTIN_DEFAULT)) {
            return PET_RESULT_STORAGE;
        }
        *reselected = true;
    }
    return ops->remove(ops->ctx, id) ? PET_RESULT_OK : PET_RESULT_STORAGE;
}

pet_source_t pet_install_resolve(const pet_store_ops_t *ops, const char *selected, uint8_t *buf, size_t cap,
                                 const uint8_t **bytes, size_t *len)
{
    const pet_builtin_t *b = selected != NULL ? pet_builtin_find(selected) : NULL;
    if (b != NULL) {
        *bytes = b->bytes;
        *len = b->len;
        return PET_SOURCE_BUILTIN;
    }
    size_t n = 0;
    if (selected != NULL && pet_pack_id_ok(selected) && ops->slots > 0 && stored_valid(ops, selected, buf, cap, &n)) {
        *bytes = buf;
        *len = n;
        return PET_SOURCE_STORED;
    }
    b = pet_builtin_find(PET_BUILTIN_DEFAULT);
    *bytes = b->bytes;
    *len = b->len;
    return PET_SOURCE_FALLBACK;
}

bool pet_part_erase_allowed(const char *label)
{
    return label != NULL && strcmp(label, PET_STORE_LABEL) == 0;
}

pet_part_state_t pet_part_init(const pet_part_ops_t *ops)
{
    const int rc = ops->init(ops->ctx, PET_STORE_LABEL);
    if (rc == 0) {
        return PET_PART_OK;
    }
    if (rc == 1) {
        return PET_PART_ABSENT;
    }
    if (!ops->erase(ops->ctx, PET_STORE_LABEL)) {
        return PET_PART_FAILED;
    }
    return ops->init(ops->ctx, PET_STORE_LABEL) == 0 ? PET_PART_ERASED : PET_PART_FAILED;
}
