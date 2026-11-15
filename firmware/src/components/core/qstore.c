#include "qstore.h"

#include "text.h"

#include <string.h>

void qstore_init(qstore_t *s, qstore_slot_t *slots)
{
    s->slots = slots;
    qstore_clear(s);
}

void qstore_clear(qstore_t *s)
{
    for (size_t i = 0; i < QSTORE_SLOTS; i++) {
        s->slots[i].kind = QSTORE_FREE;
        s->slots[i].id[0] = '\0';
    }
}

qstore_slot_t *qstore_find(qstore_t *s, const char *id)
{
    for (size_t i = 0; id != NULL && i < QSTORE_SLOTS; i++) {
        if (s->slots[i].kind != QSTORE_FREE && strcmp(s->slots[i].id, id) == 0) {
            return &s->slots[i];
        }
    }
    return NULL;
}

/* A free slot set up for id, or NULL when every slot is taken. */
static qstore_slot_t *take(qstore_t *s, qstore_kind_t kind, const char *id, const char *sid, const char *project,
                           uint32_t now_ms)
{
    for (size_t i = 0; i < QSTORE_SLOTS; i++) {
        qstore_slot_t *slot = &s->slots[i];
        if (slot->kind != QSTORE_FREE) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        slot->kind = kind;
        memcpy(slot->id, id, sizeof(slot->id));
        slot->id[sizeof(slot->id) - 1] = '\0';
        text_sanitize_ascii(slot->sid, sizeof(slot->sid), sid);
        text_sanitize_device(slot->project, sizeof(slot->project), project);
        slot->first_ms = now_ms;
        return slot;
    }
    return NULL;
}

static void set_timeout(qstore_slot_t *slot, uint32_t timeout_ms, uint32_t now_ms)
{
    slot->timeout_ms = timeout_ms;
    slot->deadline_ms = now_ms + timeout_ms;
}

qstore_rc_t qstore_add_question(qstore_t *s, const proto_question_t *q, uint32_t now_ms)
{
    if (q->index >= q->count || q->count > PROTO_Q_MAX || q->n_options > PROTO_Q_OPTIONS_MAX) {
        return QSTORE_MISMATCH;
    }
    qstore_slot_t *slot = qstore_find(s, q->id);
    if (slot != NULL) {
        if (slot->kind != QSTORE_QUESTION || slot->count != q->count) {
            return QSTORE_MISMATCH;
        }
        if (slot->complete || (slot->received & (1u << q->index)) != 0) {
            return QSTORE_DUPLICATE;
        }
    } else {
        slot = take(s, QSTORE_QUESTION, q->id, q->sid, q->project, now_ms);
        if (slot == NULL) {
            return QSTORE_FULL;
        }
        slot->count = q->count;
    }
    qstore_question_t *dst = &slot->u.q[q->index];
    dst->has_header = q->has_header;
    text_sanitize_device(dst->header, sizeof(dst->header), q->has_header ? q->header : "");
    text_sanitize_device(dst->text, sizeof(dst->text), q->text);
    dst->multi = q->multi;
    dst->n_options = q->n_options;
    for (uint8_t i = 0; i < q->n_options; i++) {
        text_sanitize_device(dst->options[i].label, sizeof(dst->options[i].label), q->options[i].label);
        text_sanitize_device(dst->options[i].desc, sizeof(dst->options[i].desc), q->options[i].desc);
    }
    slot->received |= (uint8_t)(1u << q->index);
    set_timeout(slot, q->timeout_ms, now_ms);
    slot->complete = slot->received == (uint8_t)((1u << slot->count) - 1);
    return slot->complete ? QSTORE_COMPLETE : QSTORE_PARTIAL;
}

qstore_rc_t qstore_add_plan(qstore_t *s, const proto_plan_t *p, uint32_t now_ms)
{
    qstore_slot_t *slot = qstore_find(s, p->id);
    if (slot != NULL) {
        return slot->kind == QSTORE_PLAN ? QSTORE_DUPLICATE : QSTORE_MISMATCH;
    }
    slot = take(s, QSTORE_PLAN, p->id, p->sid, p->project, now_ms);
    if (slot == NULL) {
        return QSTORE_FULL;
    }
    qstore_plan_t *dst = &slot->u.plan;
    text_sanitize_device(dst->title, sizeof(dst->title), p->title);
    dst->n_steps = p->n_steps <= PROTO_PLAN_STEPS_MAX ? p->n_steps : PROTO_PLAN_STEPS_MAX;
    for (uint8_t i = 0; i < dst->n_steps; i++) {
        text_sanitize_device(dst->steps[i], sizeof(dst->steps[i]), p->steps[i]);
    }
    dst->more = p->more;
    dst->cut = p->cut || p->more > 0;
    slot->count = 1;
    slot->received = 1;
    slot->complete = true;
    set_timeout(slot, p->timeout_ms, now_ms);
    return QSTORE_COMPLETE;
}

bool qstore_release(qstore_t *s, const char *id)
{
    qstore_slot_t *slot = qstore_find(s, id);
    if (slot == NULL) {
        return false;
    }
    slot->kind = QSTORE_FREE;
    slot->id[0] = '\0';
    return true;
}

bool qstore_expire(qstore_t *s, uint32_t now_ms, char out_id[PROTO_ID_SIZE])
{
    for (size_t i = 0; i < QSTORE_SLOTS; i++) {
        qstore_slot_t *slot = &s->slots[i];
        if (slot->kind != QSTORE_FREE && !slot->complete &&
            (int32_t)(now_ms - (slot->first_ms + QSTORE_ASSEMBLE_MS)) >= 0) {
            memcpy(out_id, slot->id, PROTO_ID_SIZE);
            slot->kind = QSTORE_FREE;
            slot->id[0] = '\0';
            return true;
        }
    }
    return false;
}

bool qstore_next_expiry(const qstore_t *s, uint32_t now_ms, uint32_t *out)
{
    bool found = false;
    for (size_t i = 0; i < QSTORE_SLOTS; i++) {
        const qstore_slot_t *slot = &s->slots[i];
        if (slot->kind == QSTORE_FREE || slot->complete) {
            continue;
        }
        const uint32_t at = slot->first_ms + QSTORE_ASSEMBLE_MS;
        if (!found || (int32_t)(at - now_ms) < (int32_t)(*out - now_ms)) {
            *out = at;
            found = true;
        }
    }
    return found;
}

size_t qstore_used(const qstore_t *s)
{
    size_t n = 0;
    for (size_t i = 0; i < QSTORE_SLOTS; i++) {
        n += s->slots[i].kind != QSTORE_FREE ? 1 : 0;
    }
    return n;
}

const char *qstore_rc_name(qstore_rc_t rc)
{
    switch (rc) {
    case QSTORE_PARTIAL:
        return "partial";
    case QSTORE_COMPLETE:
        return "complete";
    case QSTORE_DUPLICATE:
        return "duplicate";
    case QSTORE_FULL:
        return "full";
    case QSTORE_MISMATCH:
        return "mismatch";
    }
    return "";
}
