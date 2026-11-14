#include "reqq.h"

#include "text.h"

#include <string.h>

/* Deadlines wrap with the millisecond clock, so they are compared by signed distance. */
static bool passed(uint32_t deadline_ms, uint32_t now_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

void reqq_init(reqq_t *q)
{
    memset(q, 0, sizeof(*q));
}

static int index_of(const reqq_t *q, const char *id)
{
    if (id == NULL) {
        return -1;
    }
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

reqq_add_t reqq_add(reqq_t *q, const proto_permission_t *msg, uint32_t now_ms)
{
    if (index_of(q, msg->id) >= 0) {
        return REQQ_DUPLICATE;
    }
    if (q->count >= REQQ_CAP) {
        return REQQ_FULL;
    }
    reqq_entry_t *e = &q->entries[q->count++];
    memset(e, 0, sizeof(*e));
    memcpy(e->id, msg->id, sizeof(e->id));
    e->id[sizeof(e->id) - 1] = '\0';
    text_sanitize_ascii(e->tool, sizeof(e->tool), msg->tool);
    text_sanitize_ascii(e->summary, sizeof(e->summary), msg->summary);
    text_sanitize_ascii(e->details, sizeof(e->details), msg->details);
    text_sanitize_ascii(e->project, sizeof(e->project), msg->project);
    text_sanitize_ascii(e->sid, sizeof(e->sid), msg->sid);
    e->risk = msg->risk;
    e->timeout_ms = msg->timeout_ms;
    e->deadline_ms = now_ms + msg->timeout_ms;
    return REQQ_ADDED;
}

static void remove_at(reqq_t *q, size_t i)
{
    memmove(&q->entries[i], &q->entries[i + 1], (q->count - i - 1) * sizeof(q->entries[0]));
    q->count--;
    memset(&q->entries[q->count], 0, sizeof(q->entries[0]));
}

bool reqq_remove(reqq_t *q, const char *id)
{
    const int i = index_of(q, id);
    if (i < 0) {
        return false;
    }
    remove_at(q, (size_t)i);
    return true;
}

reqq_entry_t *reqq_head(reqq_t *q)
{
    return q->count > 0 ? &q->entries[0] : NULL;
}

reqq_entry_t *reqq_find(reqq_t *q, const char *id)
{
    const int i = index_of(q, id);
    return i >= 0 ? &q->entries[i] : NULL;
}

size_t reqq_count(const reqq_t *q)
{
    return q->count;
}

bool reqq_expire(reqq_t *q, uint32_t now_ms, char out_id[PROTO_ID_SIZE])
{
    for (size_t i = 0; i < q->count; i++) {
        if (passed(q->entries[i].deadline_ms, now_ms)) {
            memcpy(out_id, q->entries[i].id, PROTO_ID_SIZE);
            remove_at(q, i);
            return true;
        }
    }
    return false;
}

bool reqq_next_deadline(const reqq_t *q, uint32_t now_ms, uint32_t *out)
{
    if (q->count == 0) {
        return false;
    }
    uint32_t best = q->entries[0].deadline_ms;
    for (size_t i = 1; i < q->count; i++) {
        if ((int32_t)(q->entries[i].deadline_ms - now_ms) < (int32_t)(best - now_ms)) {
            best = q->entries[i].deadline_ms;
        }
    }
    *out = best;
    return true;
}

size_t reqq_ids(const reqq_t *q, char out[][PROTO_ID_SIZE], size_t n)
{
    size_t i = 0;
    for (; i < q->count && i < n; i++) {
        memcpy(out[i], q->entries[i].id, PROTO_ID_SIZE);
    }
    return i;
}

bool reqq_has_unacked(const reqq_t *q)
{
    for (size_t i = 0; i < q->count; i++) {
        if (!q->entries[i].acked) {
            return true;
        }
    }
    return false;
}

void reqq_mark_all_acked(reqq_t *q)
{
    for (size_t i = 0; i < q->count; i++) {
        q->entries[i].acked = true;
    }
}

void reqq_clear(reqq_t *q)
{
    reqq_init(q);
}
