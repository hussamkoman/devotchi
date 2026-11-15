#pragma once

#include "pet_pack.h"
#include "proto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Stored pets (the `pets` partition), on top of the built-ins. */
#define PET_STORE_SLOTS  8
/** Label of the partition the firmware may erase; the only one. */
#define PET_STORE_LABEL  "pets"

/** Outcome of a pet_cmd; the names are the `error` values of protocol 14.8. */
typedef enum {
    PET_RESULT_OK,
    PET_RESULT_HTTP,
    PET_RESULT_SIZE,
    PET_RESULT_CRC,
    PET_RESULT_FORMAT,
    PET_RESULT_MISMATCH,
    PET_RESULT_FULL,
    PET_RESULT_UNKNOWN,
    PET_RESULT_BUILTIN,
    PET_RESULT_BUSY,
    PET_RESULT_STORAGE,
} pet_result_t;

const char *pet_result_name(pet_result_t r);

/** A pet_cmd that passed pet_cmd_check: every field is in range. */
typedef struct {
    proto_pet_op_t op;
    char id[PET_PACK_ID_LEN + 1];
    uint8_t rev;
    uint16_t size;
    uint32_t crc32;
    bool select;
} pet_cmd_t;

/**
 * Checks a received pet_cmd before anything else happens (protocol 14.8):
 * `pet` is [a-z0-9]{1,8} byte by byte; for install also rev 1..255, size
 * 36..2048 and crc32 eight lowercase hex digits. Returns PET_RESULT_UNKNOWN
 * for a malformed command, PET_RESULT_BUILTIN for install or remove of a
 * compiled-in id, PET_RESULT_OK otherwise (out filled; out->id is built
 * from the checked bytes only).
 */
pet_result_t pet_cmd_check(const proto_pet_cmd_t *in, pet_cmd_t *out);

/** What the HTTP fetch delivered. content_length is -1 when the header was missing. */
typedef struct {
    int status;
    int32_t content_length;
    const uint8_t *body;
    size_t received;
    /** The transfer ended normally (not cut off, not timed out). */
    bool complete;
} pet_download_t;

/**
 * The download checks in protocol order: HTTP 200 and Content-Length = size
 * (http / size); exactly size bytes (http); CRC of the body = crc32 = the
 * trailer (crc); structure (format); header id and rev = the command's (mismatch).
 */
pet_result_t pet_download_check(const pet_cmd_t *cmd, const pet_download_t *dl);

/**
 * The storage the install steps go through. Every call returns false on a
 * failure; get returns the stored length (0: missing or unreadable). The
 * firmware implements it on the `pets` NVS partition and the `s_pet` key;
 * the host test uses a fake that fails or "loses power" at any step.
 */
typedef struct {
    void *ctx;
    bool (*put)(void *ctx, const char *id, const uint8_t *bytes, size_t len);
    size_t (*get)(void *ctx, const char *id, uint8_t *buf, size_t cap);
    bool (*remove)(void *ctx, const char *id);
    bool (*has)(void *ctx, const char *id);
    /** Number of stored packs. */
    size_t (*count)(void *ctx);
    /** Writes s_pet. */
    bool (*select)(void *ctx, const char *id);
    /** Slots the partition offers (0 without a `pets` partition). */
    size_t slots;
} pet_store_ops_t;

/**
 * Install order of architecture 7.2: (the pack already passed
 * pet_download_check) put -> read back into scratch and validate -> select
 * when asked. A failure leaves the store as it was (protocol 14.8): a key
 * that did not exist is removed again, a pack of the same id that existed
 * is saved in backup first and written back. *selected tells whether s_pet
 * was written.
 */
pet_result_t pet_install_commit(const pet_store_ops_t *ops, const pet_cmd_t *cmd, const uint8_t *pack, size_t len,
                                uint8_t *scratch, size_t scratch_cap, uint8_t *backup, size_t backup_cap,
                                bool *selected);

/** `select`: a built-in, or a stored pack that validates (`unknown` otherwise). */
pet_result_t pet_install_select(const pet_store_ops_t *ops, const char *id, uint8_t *scratch, size_t scratch_cap);

/** `remove`: a stored pet (`builtin`, `unknown`); when it was the selected one, Devo is selected. */
pet_result_t pet_install_remove(const pet_store_ops_t *ops, const char *id, const char *selected, bool *reselected);

typedef enum {
    PET_SOURCE_BUILTIN,
    PET_SOURCE_STORED,
    /** s_pet names a pet that is missing or does not validate: Devo is shown, s_pet is kept. */
    PET_SOURCE_FALLBACK,
} pet_source_t;

/**
 * The pack to show at boot for s_pet: a built-in, a stored pack copied into
 * buf that validates, or Devo. *bytes and *len point at the pack to decode.
 */
pet_source_t pet_install_resolve(const pet_store_ops_t *ops, const char *selected, uint8_t *buf, size_t cap,
                                 const uint8_t **bytes, size_t *len);

/** Partition operations of the `pets` init wrapper (nvs_flash_init_partition / erase_partition). */
typedef struct {
    void *ctx;
    /** 0 ok, 1 the partition does not exist, any other value a failure. */
    int (*init)(void *ctx, const char *label);
    bool (*erase)(void *ctx, const char *label);
} pet_part_ops_t;

typedef enum {
    PET_PART_OK,
    /** init failed, the partition was erased by label and init then worked. */
    PET_PART_ERASED,
    /** No `pets` partition (an older table): 0 slots. */
    PET_PART_ABSENT,
    /** init failed again after the erase: 0 slots. */
    PET_PART_FAILED,
} pet_part_state_t;

const char *pet_part_state_name(pet_part_state_t s);

/** True only for PET_STORE_LABEL: the one partition the firmware ever erases. */
bool pet_part_erase_allowed(const char *label);

/** init; on a failure erase PET_STORE_LABEL once and init again. Never erases anything else. */
pet_part_state_t pet_part_init(const pet_part_ops_t *ops);
