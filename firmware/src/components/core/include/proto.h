#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_VERSION          2
/** Largest frame either side may send (protocol section 11). */
#define PROTO_MAX_FRAME        2048
#define PROTO_TELEMETRY_MAX    300

/* Buffer sizes are the protocol byte limits plus the terminator. */
#define PROTO_ID_LEN           32
#define PROTO_ID_SIZE          (PROTO_ID_LEN + 1)
#define PROTO_TOOL_SIZE        41
#define PROTO_SUMMARY_SIZE     201
#define PROTO_DETAILS_SIZE     601
/* Protocol 14.10. */
#define PROTO_CMD_SIZE         601
#define PROTO_DESC_SIZE        201
#define PROTO_RISK_REASON_SIZE 41
#define PROTO_RULE_SIZE        121
#define PROTO_MSG_SIZE         201
#define PROTO_DETAIL_SIZE      301
#define PROTO_REASON_SIZE      201
#define PROTO_EVENT_SIZE       41
#define PROTO_PROJECT_SIZE     33
#define PROTO_SOURCE_SIZE      33
#define PROTO_SID_SIZE         9
#define PROTO_NAME_SIZE        25
#define PROTO_ROOM_SIZE        13
#define PROTO_CODE_SIZE        25
#define PROTO_DEVICE_ID_SIZE   25
#define PROTO_HOLDING_MAX_IDS  8

/** Capabilities of protocol 14.1, as bits of a mask. */
#define PROTO_CAP_DECIDE  (1u << 0)
#define PROTO_CAP_ASK     (1u << 1)
#define PROTO_CAP_PLAN    (1u << 2)
#define PROTO_CAP_PETS    (1u << 3)
#define PROTO_CAP_COUNT   4

#define PROTO_IDENTIFY_MIN_MS      1000
#define PROTO_IDENTIFY_MAX_MS      10000
#define PROTO_IDENTIFY_DEFAULT_MS  3000

typedef enum {
    /** A well-formed message of a type this firmware does not know; ignored. */
    PROTO_UNKNOWN,
    PROTO_REGISTER_ACK,
    PROTO_PING,
    PROTO_PONG,
    PROTO_ERROR,
    PROTO_PEERS,
    PROTO_STATUS,
    PROTO_NOTIFY,
    PROTO_MOOD,
    PROTO_PERMISSION,
    PROTO_DISMISS,
    PROTO_MODE,
    PROTO_IDENTIFY,
} proto_type_t;

typedef enum {
    MOOD_HAPPY,
    MOOD_SAD,
    MOOD_EXCITED,
    MOOD_ANXIOUS,
    MOOD_SLEEPING,
    MOOD_ANGRY,
    MOOD_PROUD,
    MOOD_IDLE,
    MOOD_FOCUSED,
    MOOD_WORKING,
    MOOD_THINKING,
    MOOD_COUNT,
} proto_mood_t;

typedef enum {
    PROTO_RISK_LOW,
    PROTO_RISK_MED,
    PROTO_RISK_HIGH,
} proto_risk_t;

typedef enum {
    PROTO_DISMISS_ANSWERED,
    PROTO_DISMISS_CANCELLED,
    PROTO_DISMISS_TIMEOUT,
    PROTO_DISMISS_RELEASED,
} proto_dismiss_reason_t;

typedef enum {
    PROTO_OUTCOME_NONE,
    PROTO_OUTCOME_ALLOW,
    PROTO_OUTCOME_DENY,
} proto_outcome_t;

/** `decide` choices (protocol 14.3); `dismiss.choice` adds ANSWERED. */
typedef enum {
    PROTO_CHOICE_NONE,
    PROTO_CHOICE_ALLOW_SESSION,
    PROTO_CHOICE_DENY_ASK_WHY,
    PROTO_CHOICE_DENY_STOP,
    PROTO_CHOICE_APPROVE,
    PROTO_CHOICE_APPROVE_ACCEPT_EDITS,
    PROTO_CHOICE_REVISE,
    PROTO_CHOICE_DECLINE,
    PROTO_CHOICE_ANSWERED,
    PROTO_CHOICE_COUNT,
} proto_choice_t;

typedef enum {
    PROTO_MODE_ASK,
    PROTO_MODE_DESK,
} proto_mode_t;

typedef struct {
    uint16_t producers;
    uint16_t consumers;
    uint16_t devices;
} proto_peers_t;

typedef struct {
    int32_t proto;
    proto_mode_t mode;
    /** Epoch milliseconds, 0 when absent. */
    int64_t server_time_ms;
    proto_peers_t peers;
    char room[PROTO_ROOM_SIZE];
    char device_id[PROTO_DEVICE_ID_SIZE];
    char name[PROTO_NAME_SIZE];
    /** PROTO_CAP_* the server listed; 0 for a phase-1 server. */
    uint32_t caps;
} proto_register_ack_t;

typedef struct {
    char code[PROTO_CODE_SIZE];
    char message[PROTO_MSG_SIZE];
    bool has_id;
    char id[PROTO_ID_SIZE];
} proto_error_t;

typedef struct {
    char msg[PROTO_MSG_SIZE];
    /** False means the sender left the mood out and the current one stays. */
    bool has_mood;
    proto_mood_t mood;
    char sid[PROTO_SID_SIZE];
    char project[PROTO_PROJECT_SIZE];
} proto_status_t;

typedef struct {
    char event[PROTO_EVENT_SIZE];
    proto_mood_t mood;
    char detail[PROTO_DETAIL_SIZE];
    uint8_t urgency;
    char source[PROTO_SOURCE_SIZE];
    char sid[PROTO_SID_SIZE];
    char project[PROTO_PROJECT_SIZE];
} proto_notify_t;

typedef struct {
    proto_mood_t mood;
    char reason[PROTO_REASON_SIZE];
} proto_mood_msg_t;

typedef struct {
    char id[PROTO_ID_SIZE];
    char tool[PROTO_TOOL_SIZE];
    char summary[PROTO_SUMMARY_SIZE];
    char details[PROTO_DETAILS_SIZE];
    proto_risk_t risk;
    char sid[PROTO_SID_SIZE];
    char project[PROTO_PROJECT_SIZE];
    /** Remaining hold time when the server sent the message. */
    uint32_t timeout_ms;
    /* Card fields (protocol 14.2), sent only to consumers with `decide`. */
    bool has_cmd;
    char cmd[PROTO_CMD_SIZE];
    /** The server shortened cmd, or cmd did not fit its buffer: no allow from this device. */
    bool cut;
    char desc[PROTO_DESC_SIZE];
    char reason[PROTO_RISK_REASON_SIZE];
    /**
     * A session grant may be offered. Never together with cut; a grant that
     * cannot be shown word for word (too long, not printable ASCII, unknown
     * match) is dropped.
     */
    bool has_grant;
    char grant_rule[PROTO_RULE_SIZE];
    bool grant_prefix;
} proto_permission_t;

typedef struct {
    char id[PROTO_ID_SIZE];
    proto_dismiss_reason_t reason;
    proto_outcome_t outcome;
    char by[PROTO_NAME_SIZE];
    /** PROTO_CHOICE_NONE for a plain permission_response or an unknown choice. */
    proto_choice_t choice;
} proto_dismiss_t;

typedef struct {
    proto_type_t type;
    union {
        proto_register_ack_t register_ack;
        proto_error_t error;
        proto_peers_t peers;
        proto_status_t status;
        proto_notify_t notify;
        proto_mood_msg_t mood;
        proto_permission_t permission;
        proto_dismiss_t dismiss;
        proto_mode_t mode;
        uint32_t identify_ms;
    } u;
} proto_msg_t;

typedef struct {
    /** `charging`, `full`, `discharging` or `absent`. */
    const char *battery_state;
    /** Negative for "no percentage" (sent as null). */
    int pct;
    int mv;
    bool ext_power;
    int rssi;
    uint32_t uptime_s;
    uint32_t heap_free;
    uint32_t heap_min;
    const char *reset_reason;
    const char *fw;
    /** Optional fields: NULL leaves them out. */
    const char *screen;
    const char *stage;
    const char *alert;
} proto_telemetry_t;

/**
 * Parses one inbound frame. False for anything that is not a JSON object with
 * a string `type`, or whose known type has a missing or wrong-typed field.
 * Strings longer than their buffer are cut on a code point boundary.
 */
bool proto_parse(const char *json, size_t len, proto_msg_t *out);

const char *proto_type_name(proto_type_t type);
const char *proto_mood_name(proto_mood_t mood);
const char *proto_mode_name(proto_mode_t mode);
const char *proto_choice_name(proto_choice_t choice);

/** Unknown names map to MOOD_IDLE (protocol 5.1). */
proto_mood_t proto_mood_from_name(const char *name);

/** True for exactly 32 lowercase hex characters. */
bool proto_id_is_valid(const char *id);

/* Serialisers write a NUL-terminated frame and return its length, or -1 when it does not fit. */

/** caps: PROTO_CAP_* this consumer can present; 0 leaves `client.caps` out. */
int proto_write_register(char *buf, size_t cap, const char *device_id, const char *model, const char *version,
                         uint32_t caps);
int proto_write_pong(char *buf, size_t cap);
int proto_write_holding(char *buf, size_t cap, const char ids[][PROTO_ID_SIZE], size_t count);
int proto_write_permission_response(char *buf, size_t cap, const char *id, bool granted);
int proto_write_permission_defer(char *buf, size_t cap, const char *id);
/** Fails for PROTO_CHOICE_NONE and ANSWERED, which are not `decide` choices. */
int proto_write_decide(char *buf, size_t cap, const char *id, proto_choice_t choice);
int proto_write_set_mode(char *buf, size_t cap, proto_mode_t mode);

/** Also fails when the frame would exceed PROTO_TELEMETRY_MAX bytes. */
int proto_write_telemetry(char *buf, size_t cap, const proto_telemetry_t *t);
