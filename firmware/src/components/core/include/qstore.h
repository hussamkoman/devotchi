#pragma once

#include "proto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Questions and plans the device holds at once (protocol 14.10); others are not named in `holding`. */
#define QSTORE_SLOTS        2
/** The frames of a question call arrive back to back; a call still incomplete after this is dropped. */
#define QSTORE_ASSEMBLE_MS  2000

typedef enum {
    QSTORE_FREE,
    QSTORE_QUESTION,
    QSTORE_PLAN,
} qstore_kind_t;

/** Text fields are device text (text_sanitize_device). */
typedef struct {
    bool has_header;
    char header[PROTO_Q_HEADER_SIZE];
    char text[PROTO_Q_TEXT_SIZE];
    bool multi;
    uint8_t n_options;
    proto_option_t options[PROTO_Q_OPTIONS_MAX];
} qstore_question_t;

typedef struct {
    char title[PROTO_PLAN_TITLE_SIZE];
    uint8_t n_steps;
    char steps[PROTO_PLAN_STEPS_MAX][PROTO_PLAN_STEP_SIZE];
    uint16_t more;
    bool cut;
} qstore_plan_t;

typedef struct {
    qstore_kind_t kind;
    char id[PROTO_ID_SIZE];
    char sid[PROTO_SID_SIZE];
    char project[PROTO_PROJECT_SIZE];
    /** Questions of the call, and a bit per question whose frame arrived. */
    uint8_t count;
    uint8_t received;
    bool complete;
    /** When the first frame arrived (the assembly limit runs from it). */
    uint32_t first_ms;
    /** The hold the newest frame reported, and the deadline it gives. */
    uint32_t timeout_ms;
    uint32_t deadline_ms;
    union {
        qstore_question_t q[PROTO_Q_MAX];
        qstore_plan_t plan;
    } u;
} qstore_slot_t;

/** The slots are caller memory (QSTORE_SLOTS of them), so the app can place them in PSRAM. */
typedef struct {
    qstore_slot_t *slots;
} qstore_t;

typedef enum {
    /** Stored; more frames of this call are still to come. */
    QSTORE_PARTIAL,
    /** This frame made the request complete: present it now. */
    QSTORE_COMPLETE,
    /** A frame already stored (a replay); nothing changed. */
    QSTORE_DUPLICATE,
    /** No free slot: the request is not presented. */
    QSTORE_FULL,
    /** The frame disagrees with the frames already stored under its id (count or kind); dropped. */
    QSTORE_MISMATCH,
} qstore_rc_t;

/** slots: QSTORE_SLOTS slots; all become free. */
void qstore_init(qstore_t *s, qstore_slot_t *slots);

qstore_rc_t qstore_add_question(qstore_t *s, const proto_question_t *q, uint32_t now_ms);
qstore_rc_t qstore_add_plan(qstore_t *s, const proto_plan_t *p, uint32_t now_ms);

/** The slot holding id (complete or not), NULL when none. */
qstore_slot_t *qstore_find(qstore_t *s, const char *id);

/** Frees the slot of id; false when none holds it. */
bool qstore_release(qstore_t *s, const char *id);

void qstore_clear(qstore_t *s);

/** Frees one incomplete slot whose assembly time ran out and copies its id; false when none did. */
bool qstore_expire(qstore_t *s, uint32_t now_ms, char out_id[PROTO_ID_SIZE]);

/** When the next incomplete slot runs out; false when no slot is incomplete. */
bool qstore_next_expiry(const qstore_t *s, uint32_t now_ms, uint32_t *out);

size_t qstore_used(const qstore_t *s);

const char *qstore_rc_name(qstore_rc_t rc);
