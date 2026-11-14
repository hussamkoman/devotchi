#include "pet_model.h"

#include "text.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    const char *event;
    pet_event_class_t cls;
} event_rule_t;

static const event_rule_t EXACT[] = {
    {"tests_passed", PET_EVENT_POSITIVE},  {"build_succeeded", PET_EVENT_POSITIVE},
    {"deploy_complete", PET_EVENT_POSITIVE}, {"tests_failed", PET_EVENT_NEGATIVE},
    {"build_failed", PET_EVENT_NEGATIVE},  {"deploy_failed", PET_EVENT_NEGATIVE},
    {"error", PET_EVENT_NEGATIVE},         {"task_complete", PET_EVENT_COMPLETE},
    {"task_paused", PET_EVENT_NONE},       {"needs_input", PET_EVENT_ATTENTION},
};

static const event_rule_t SUFFIX[] = {
    {"_succeeded", PET_EVENT_POSITIVE},
    {"_passed", PET_EVENT_POSITIVE},
    {"_failed", PET_EVENT_NEGATIVE},
};

void pet_model_init(pet_model_t *m)
{
    memset(m, 0, sizeof(*m));
    m->mood = MOOD_IDLE;
}

void pet_model_apply_status(pet_model_t *m, const proto_status_t *msg)
{
    text_sanitize_ascii(m->status, sizeof(m->status), msg->msg);
    if (msg->project[0] != '\0') {
        text_sanitize_ascii(m->project, sizeof(m->project), msg->project);
    }
    if (msg->has_mood) {
        m->mood = msg->mood;
    }
}

void pet_model_apply_mood(pet_model_t *m, const proto_mood_msg_t *msg)
{
    m->mood = msg->mood;
}

pet_event_class_t pet_model_apply_notify(pet_model_t *m, const proto_notify_t *msg)
{
    m->mood = msg->mood;
    text_sanitize_ascii(m->last_event, sizeof(m->last_event), msg->event);
    text_sanitize_ascii(m->last_detail, sizeof(m->last_detail), msg->detail);
    if (msg->project[0] != '\0') {
        text_sanitize_ascii(m->project, sizeof(m->project), msg->project);
    }
    return pet_event_class(msg->event, msg->mood);
}

static bool ends_with(const char *s, const char *suffix)
{
    const size_t n = strlen(s);
    const size_t k = strlen(suffix);
    return n > k && strcmp(s + n - k, suffix) == 0;
}

pet_event_class_t pet_event_class(const char *event, proto_mood_t mood)
{
    const char *e = event != NULL ? event : "";
    for (size_t i = 0; i < sizeof(EXACT) / sizeof(EXACT[0]); i++) {
        if (strcmp(e, EXACT[i].event) == 0) {
            return EXACT[i].cls;
        }
    }
    for (size_t i = 0; i < sizeof(SUFFIX) / sizeof(SUFFIX[0]); i++) {
        if (ends_with(e, SUFFIX[i].event)) {
            return SUFFIX[i].cls;
        }
    }
    switch (mood) {
    case MOOD_HAPPY:
    case MOOD_EXCITED:
    case MOOD_PROUD:
        return PET_EVENT_POSITIVE;
    case MOOD_SAD:
    case MOOD_ANGRY:
        return PET_EVENT_NEGATIVE;
    case MOOD_ANXIOUS:
    case MOOD_SLEEPING:
    case MOOD_IDLE:
    case MOOD_FOCUSED:
    case MOOD_WORKING:
    case MOOD_THINKING:
    case MOOD_COUNT:
        break;
    }
    return PET_EVENT_NONE;
}

const char *pet_event_class_name(pet_event_class_t c)
{
    switch (c) {
    case PET_EVENT_NONE:
        return "none";
    case PET_EVENT_POSITIVE:
        return "positive";
    case PET_EVENT_NEGATIVE:
        return "negative";
    case PET_EVENT_COMPLETE:
        return "complete";
    case PET_EVENT_ATTENTION:
        return "attention";
    }
    return "";
}

/* Appends src to dst at *used; stops when dst is full. */
static void append(char *dst, size_t cap, size_t *used, const char *src)
{
    for (; *src != '\0' && *used + 1 < cap; src++) {
        dst[(*used)++] = *src;
    }
    dst[*used] = '\0';
}

size_t pet_notify_text(char *dst, size_t cap, const proto_notify_t *msg)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    /* Sanitising works per character, so sanitising each piece equals sanitising the joined text. */
    size_t used = 0;
    dst[0] = '\0';
    if (msg->source[0] != '\0') {
        used += text_sanitize_ascii(dst + used, cap - used, msg->source);
        append(dst, cap, &used, ": ");
    }
    const size_t event_at = used;
    used += text_sanitize_ascii(dst + used, cap - used, msg->event);
    for (size_t i = event_at; i < used; i++) {
        if (dst[i] == '_') {
            dst[i] = ' ';
        }
    }
    if (msg->detail[0] != '\0') {
        append(dst, cap, &used, ": ");
        used += text_sanitize_ascii(dst + used, cap - used, msg->detail);
    }
    return used;
}
