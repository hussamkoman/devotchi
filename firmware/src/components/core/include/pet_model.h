#pragma once

#include "proto.h"

#include <stddef.h>

/** How the device reacts to a notification (protocol 5.2). */
typedef enum {
    PET_EVENT_NONE,
    PET_EVENT_POSITIVE,
    PET_EVENT_NEGATIVE,
    PET_EVENT_COMPLETE,
    PET_EVENT_ATTENTION,
} pet_event_class_t;

/** Text fields hold printable ASCII (text_sanitize_ascii). */
typedef struct {
    proto_mood_t mood;
    char status[PROTO_MSG_SIZE];
    char project[PROTO_PROJECT_SIZE];
    char last_event[PROTO_EVENT_SIZE];
    char last_detail[PROTO_DETAIL_SIZE];
} pet_model_t;

/** Mood idle, no texts. */
void pet_model_init(pet_model_t *m);

/** Sets the status text; the mood changes only when the message carries one (protocol 4.3). */
void pet_model_apply_status(pet_model_t *m, const proto_status_t *msg);

void pet_model_apply_mood(pet_model_t *m, const proto_mood_msg_t *msg);

/** Sets the mood and the last notification; returns the reaction class. */
pet_event_class_t pet_model_apply_notify(pet_model_t *m, const proto_notify_t *msg);

/** By event name first (exact names, then the _passed/_succeeded/_failed suffixes), then by mood. */
pet_event_class_t pet_event_class(const char *event, proto_mood_t mood);

const char *pet_event_class_name(pet_event_class_t c);

/**
 * Writes the toast for a notification: "[<source>: ]<event words>[: <detail>]",
 * underscores in the event shown as spaces, printable ASCII only. Returns the length.
 */
size_t pet_notify_text(char *dst, size_t cap, const proto_notify_t *msg);
