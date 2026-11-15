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

/** Home page 2 (recent activity) keeps this many status and notify lines. */
#define PET_RECENT_MAX        5
#define PET_RECENT_TEXT_SIZE  64

typedef struct {
    /** Monotonic time of the line, and the wall time (0 while the clock was not set). */
    uint32_t at_ms;
    int64_t epoch_ms;
    char text[PET_RECENT_TEXT_SIZE];
} pet_recent_t;

/** Text fields hold printable ASCII (text_sanitize_ascii). */
typedef struct {
    proto_mood_t mood;
    char status[PROTO_MSG_SIZE];
    char project[PROTO_PROJECT_SIZE];
    char last_event[PROTO_EVENT_SIZE];
    char last_detail[PROTO_DETAIL_SIZE];
    /** Ring of recent lines; recent_next is where the next one goes. */
    pet_recent_t recent[PET_RECENT_MAX];
    uint8_t recent_count;
    uint8_t recent_next;
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

/** Adds a line to the recent ring; an empty text, or the same text as the newest line, is not added. */
void pet_model_add_recent(pet_model_t *m, const char *text, uint32_t now_ms, int64_t epoch_ms);

/** The i-th most recent line (0 the newest); NULL past the end. */
const pet_recent_t *pet_model_recent(const pet_model_t *m, size_t i);
