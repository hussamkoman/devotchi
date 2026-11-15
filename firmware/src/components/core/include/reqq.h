#pragma once

#include "proto.h"
#include "qstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Requests the device presents at once; the room holds up to 8 (protocol 11). */
#define REQQ_CAP  4

typedef enum {
    REQQ_ADDED,
    /** The id is already queued (a replay after a reconnect); its deadline is kept. */
    REQQ_DUPLICATE,
    REQQ_FULL,
} reqq_add_t;

typedef enum {
    REQQ_PERMISSION,
    REQQ_QUESTION,
    REQQ_PLAN,
} reqq_kind_t;

/** Text fields hold printable ASCII (text_sanitize_ascii) so they can be shown and paged as they are. */
typedef struct {
    reqq_kind_t kind;
    /** The body of a question or plan; NULL for a permission. */
    const qstore_slot_t *slot;
    char id[PROTO_ID_SIZE];
    char tool[PROTO_TOOL_SIZE];
    char summary[PROTO_SUMMARY_SIZE];
    /** The exact command (`cmd`), or the phase-1 `details` when the server sent no `cmd`. */
    char cmd[PROTO_CMD_SIZE];
    /** The server sent `cmd` (protocol 14.2); otherwise cmd holds `details`. */
    bool has_cmd;
    bool cut;
    /** Claude's own description of the call. */
    char desc[PROTO_DESC_SIZE];
    /** Why the risk is med or high. */
    char reason[PROTO_RISK_REASON_SIZE];
    bool has_grant;
    char grant_rule[PROTO_RULE_SIZE];
    bool grant_prefix;
    proto_risk_t risk;
    char project[PROTO_PROJECT_SIZE];
    char sid[PROTO_SID_SIZE];
    /** Hold time the server reported at receipt; the countdown runs from it to the deadline. */
    uint32_t timeout_ms;
    uint32_t deadline_ms;
    /** Named by a holding frame. */
    bool acked;
    /** An answer was sent and its dismiss or error has not arrived yet. */
    bool answered;
    uint32_t answered_ms;
    /** The answers this device sent for a question (receipt), n_answers 0 before. */
    uint8_t n_answers;
    proto_answer_t answers[PROTO_Q_MAX];
} reqq_entry_t;

/** FIFO; entries[0] is the oldest request. */
typedef struct {
    reqq_entry_t entries[REQQ_CAP];
    size_t count;
} reqq_t;

void reqq_init(reqq_t *q);

reqq_add_t reqq_add(reqq_t *q, const proto_permission_t *msg, uint32_t now_ms);

/** A complete question or plan; its deadline is the one the slot's newest frame gave. */
reqq_add_t reqq_add_slot(reqq_t *q, const qstore_slot_t *slot);

/**
 * A `deadline` (protocol 14.6) for a queued question or plan: the hold is
 * timeout_ms from now, shorter or longer than before. False for an unknown id
 * or a permission.
 */
bool reqq_set_deadline(reqq_t *q, const char *id, uint32_t timeout_ms, uint32_t now_ms);

/** Removes the request with this id; false when it is not queued. */
bool reqq_remove(reqq_t *q, const char *id);

/** The oldest request, NULL when the queue is empty. */
reqq_entry_t *reqq_head(reqq_t *q);

reqq_entry_t *reqq_find(reqq_t *q, const char *id);

size_t reqq_count(const reqq_t *q);

/** Removes the oldest request whose deadline passed and copies its id; false when none did. */
bool reqq_expire(reqq_t *q, uint32_t now_ms, char out_id[PROTO_ID_SIZE]);

/** Earliest deadline of all queued requests; false when the queue is empty. */
bool reqq_next_deadline(const reqq_t *q, uint32_t now_ms, uint32_t *out);

/** Copies up to n ids in queue order; returns the number copied. */
size_t reqq_ids(const reqq_t *q, char out[][PROTO_ID_SIZE], size_t n);

bool reqq_has_unacked(const reqq_t *q);
void reqq_mark_all_acked(reqq_t *q);

void reqq_clear(reqq_t *q);
