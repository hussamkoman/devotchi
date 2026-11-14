#include "proto.h"

#include "text.h"

#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define URGENCY_DEFAULT  1
#define URGENCY_MAX      3

static const char *const TYPE_NAMES[] = {
    [PROTO_UNKNOWN] = "unknown",
    [PROTO_REGISTER_ACK] = "register_ack",
    [PROTO_PING] = "ping",
    [PROTO_PONG] = "pong",
    [PROTO_ERROR] = "error",
    [PROTO_PEERS] = "peers",
    [PROTO_STATUS] = "status",
    [PROTO_NOTIFY] = "notify",
    [PROTO_MOOD] = "mood",
    [PROTO_PERMISSION] = "permission",
    [PROTO_DISMISS] = "dismiss",
    [PROTO_MODE] = "mode",
    [PROTO_IDENTIFY] = "identify",
};

#define TYPE_COUNT  (sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]))

static const char *const MOOD_NAMES[MOOD_COUNT] = {
    [MOOD_HAPPY] = "happy",
    [MOOD_SAD] = "sad",
    [MOOD_EXCITED] = "excited",
    [MOOD_ANXIOUS] = "anxious",
    [MOOD_SLEEPING] = "sleeping",
    [MOOD_ANGRY] = "angry",
    [MOOD_PROUD] = "proud",
    [MOOD_IDLE] = "idle",
    [MOOD_FOCUSED] = "focused",
    [MOOD_WORKING] = "working",
    [MOOD_THINKING] = "thinking",
};

const char *proto_type_name(proto_type_t type)
{
    return (size_t)type < TYPE_COUNT ? TYPE_NAMES[type] : "unknown";
}

const char *proto_mood_name(proto_mood_t mood)
{
    return (size_t)mood < MOOD_COUNT ? MOOD_NAMES[mood] : "idle";
}

const char *proto_mode_name(proto_mode_t mode)
{
    return mode == PROTO_MODE_DESK ? "desk" : "ask";
}

proto_mood_t proto_mood_from_name(const char *name)
{
    if (name != NULL) {
        for (size_t i = 0; i < MOOD_COUNT; i++) {
            if (strcmp(name, MOOD_NAMES[i]) == 0) {
                return (proto_mood_t)i;
            }
        }
    }
    return MOOD_IDLE;
}

bool proto_id_is_valid(const char *id)
{
    if (id == NULL || strlen(id) != PROTO_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < PROTO_ID_LEN; i++) {
        const char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

/* ---- parsing ---- */

typedef enum {
    FIELD_REQUIRED,
    FIELD_OPTIONAL,
} field_rule_t;

/* Copies a string field; an absent optional field leaves dst empty. A present field of another type fails. */
static bool get_str(const cJSON *obj, const char *key, field_rule_t rule, char *dst, size_t cap)
{
    dst[0] = '\0';
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return rule == FIELD_OPTIONAL;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    text_utf8_truncate(dst, cap, item->valuestring);
    return true;
}

static bool get_num(const cJSON *obj, const char *key, field_rule_t rule, double fallback, double *out)
{
    *out = fallback;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return rule == FIELD_OPTIONAL;
    }
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static double clamp(double v, double lo, double hi)
{
    /* The negated comparison also catches NaN. */
    if (!(v >= lo)) {
        return lo;
    }
    return v > hi ? hi : v;
}

static bool get_mood(const cJSON *obj, field_rule_t rule, bool *present, proto_mood_t *out)
{
    *present = false;
    *out = MOOD_IDLE;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, "mood");
    if (item == NULL) {
        return rule == FIELD_OPTIONAL;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    *present = true;
    *out = proto_mood_from_name(item->valuestring);
    return true;
}

static bool get_id(const cJSON *obj, field_rule_t rule, bool *present, char id[PROTO_ID_SIZE])
{
    id[0] = '\0';
    *present = false;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, "id");
    if (item == NULL) {
        return rule == FIELD_OPTIONAL;
    }
    if (!cJSON_IsString(item) || !proto_id_is_valid(item->valuestring)) {
        return false;
    }
    memcpy(id, item->valuestring, PROTO_ID_SIZE);
    *present = true;
    return true;
}

static bool parse_mode_name(const char *s, proto_mode_t *out)
{
    if (strcmp(s, "ask") == 0) {
        *out = PROTO_MODE_ASK;
        return true;
    }
    if (strcmp(s, "desk") == 0) {
        *out = PROTO_MODE_DESK;
        return true;
    }
    return false;
}

static bool parse_peers(const cJSON *obj, proto_peers_t *out)
{
    double producers;
    double consumers;
    double devices;
    if (!get_num(obj, "producers", FIELD_OPTIONAL, 0, &producers) ||
        !get_num(obj, "consumers", FIELD_OPTIONAL, 0, &consumers) ||
        !get_num(obj, "devices", FIELD_OPTIONAL, 0, &devices)) {
        return false;
    }
    out->producers = (uint16_t)clamp(producers, 0, UINT16_MAX);
    out->consumers = (uint16_t)clamp(consumers, 0, UINT16_MAX);
    out->devices = (uint16_t)clamp(devices, 0, UINT16_MAX);
    return true;
}

static bool parse_register_ack(const cJSON *obj, proto_register_ack_t *out)
{
    double proto;
    double server_time;
    char mode[8];
    if (!get_num(obj, "proto", FIELD_REQUIRED, 0, &proto) ||
        !get_num(obj, "serverTime", FIELD_OPTIONAL, 0, &server_time) ||
        !get_str(obj, "roomId", FIELD_OPTIONAL, out->room, sizeof(out->room)) ||
        !get_str(obj, "mode", FIELD_OPTIONAL, mode, sizeof(mode))) {
        return false;
    }
    out->proto = (int32_t)clamp(proto, INT32_MIN, INT32_MAX);
    out->server_time_ms = (int64_t)clamp(server_time, 0, 9.0e15);
    out->mode = PROTO_MODE_ASK;
    parse_mode_name(mode, &out->mode);

    const cJSON *peers = cJSON_GetObjectItemCaseSensitive(obj, "peers");
    if (peers != NULL && (!cJSON_IsObject(peers) || !parse_peers(peers, &out->peers))) {
        return false;
    }
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(obj, "device");
    if (device != NULL) {
        if (!cJSON_IsObject(device) ||
            !get_str(device, "id", FIELD_OPTIONAL, out->device_id, sizeof(out->device_id)) ||
            !get_str(device, "name", FIELD_OPTIONAL, out->name, sizeof(out->name))) {
            return false;
        }
    }
    return true;
}

static bool parse_error(const cJSON *obj, proto_error_t *out)
{
    return get_str(obj, "code", FIELD_REQUIRED, out->code, sizeof(out->code)) &&
           get_str(obj, "message", FIELD_OPTIONAL, out->message, sizeof(out->message)) &&
           get_id(obj, FIELD_OPTIONAL, &out->has_id, out->id);
}

static bool parse_status(const cJSON *obj, proto_status_t *out)
{
    return get_str(obj, "msg", FIELD_REQUIRED, out->msg, sizeof(out->msg)) &&
           get_mood(obj, FIELD_OPTIONAL, &out->has_mood, &out->mood) &&
           get_str(obj, "sid", FIELD_OPTIONAL, out->sid, sizeof(out->sid)) &&
           get_str(obj, "project", FIELD_OPTIONAL, out->project, sizeof(out->project));
}

static bool parse_notify(const cJSON *obj, proto_notify_t *out)
{
    bool has_mood;
    double urgency;
    if (!get_str(obj, "event", FIELD_REQUIRED, out->event, sizeof(out->event)) ||
        !get_mood(obj, FIELD_REQUIRED, &has_mood, &out->mood) ||
        !get_str(obj, "detail", FIELD_OPTIONAL, out->detail, sizeof(out->detail)) ||
        !get_num(obj, "urgency", FIELD_OPTIONAL, URGENCY_DEFAULT, &urgency) ||
        !get_str(obj, "source", FIELD_OPTIONAL, out->source, sizeof(out->source)) ||
        !get_str(obj, "sid", FIELD_OPTIONAL, out->sid, sizeof(out->sid)) ||
        !get_str(obj, "project", FIELD_OPTIONAL, out->project, sizeof(out->project))) {
        return false;
    }
    out->urgency = (uint8_t)clamp(urgency, 0, URGENCY_MAX);
    return true;
}

static bool parse_mood(const cJSON *obj, proto_mood_msg_t *out)
{
    bool has_mood;
    return get_mood(obj, FIELD_REQUIRED, &has_mood, &out->mood) &&
           get_str(obj, "reason", FIELD_OPTIONAL, out->reason, sizeof(out->reason));
}

static bool parse_permission(const cJSON *obj, proto_permission_t *out)
{
    bool has_id;
    char risk[8];
    double timeout;
    if (!get_id(obj, FIELD_REQUIRED, &has_id, out->id) ||
        !get_str(obj, "tool", FIELD_REQUIRED, out->tool, sizeof(out->tool)) ||
        !get_str(obj, "summary", FIELD_REQUIRED, out->summary, sizeof(out->summary)) ||
        !get_str(obj, "details", FIELD_OPTIONAL, out->details, sizeof(out->details)) ||
        !get_str(obj, "risk", FIELD_REQUIRED, risk, sizeof(risk)) ||
        !get_str(obj, "sid", FIELD_OPTIONAL, out->sid, sizeof(out->sid)) ||
        !get_str(obj, "project", FIELD_OPTIONAL, out->project, sizeof(out->project)) ||
        !get_num(obj, "timeout", FIELD_REQUIRED, 0, &timeout)) {
        return false;
    }
    if (!(timeout >= 1)) {
        return false;
    }
    out->timeout_ms = (uint32_t)clamp(timeout, 1, UINT32_MAX);
    if (strcmp(risk, "low") == 0) {
        out->risk = PROTO_RISK_LOW;
    } else if (strcmp(risk, "high") == 0) {
        out->risk = PROTO_RISK_HIGH;
    } else {
        out->risk = PROTO_RISK_MED;
    }
    return true;
}

static bool parse_dismiss(const cJSON *obj, proto_dismiss_t *out)
{
    bool has_id;
    char reason[12];
    char outcome[8];
    if (!get_id(obj, FIELD_REQUIRED, &has_id, out->id) ||
        !get_str(obj, "reason", FIELD_REQUIRED, reason, sizeof(reason)) ||
        !get_str(obj, "outcome", FIELD_OPTIONAL, outcome, sizeof(outcome)) ||
        !get_str(obj, "by", FIELD_OPTIONAL, out->by, sizeof(out->by))) {
        return false;
    }
    if (strcmp(reason, "answered") == 0) {
        out->reason = PROTO_DISMISS_ANSWERED;
    } else if (strcmp(reason, "cancelled") == 0) {
        out->reason = PROTO_DISMISS_CANCELLED;
    } else if (strcmp(reason, "timeout") == 0) {
        out->reason = PROTO_DISMISS_TIMEOUT;
    } else {
        out->reason = PROTO_DISMISS_RELEASED;
    }
    if (strcmp(outcome, "allow") == 0) {
        out->outcome = PROTO_OUTCOME_ALLOW;
    } else if (strcmp(outcome, "deny") == 0) {
        out->outcome = PROTO_OUTCOME_DENY;
    } else {
        out->outcome = PROTO_OUTCOME_NONE;
    }
    return true;
}

static bool parse_mode(const cJSON *obj, proto_mode_t *out)
{
    char mode[8];
    return get_str(obj, "mode", FIELD_REQUIRED, mode, sizeof(mode)) && parse_mode_name(mode, out);
}

static bool parse_identify(const cJSON *obj, uint32_t *out)
{
    double ms;
    if (!get_num(obj, "durationMs", FIELD_OPTIONAL, PROTO_IDENTIFY_DEFAULT_MS, &ms)) {
        return false;
    }
    *out = (uint32_t)clamp(ms, PROTO_IDENTIFY_MIN_MS, PROTO_IDENTIFY_MAX_MS);
    return true;
}

static bool parse_body(const cJSON *root, proto_msg_t *out)
{
    switch (out->type) {
    case PROTO_UNKNOWN:
    case PROTO_PING:
    case PROTO_PONG:
        return true;
    case PROTO_REGISTER_ACK:
        return parse_register_ack(root, &out->u.register_ack);
    case PROTO_ERROR:
        return parse_error(root, &out->u.error);
    case PROTO_PEERS:
        return parse_peers(root, &out->u.peers);
    case PROTO_STATUS:
        return parse_status(root, &out->u.status);
    case PROTO_NOTIFY:
        return parse_notify(root, &out->u.notify);
    case PROTO_MOOD:
        return parse_mood(root, &out->u.mood);
    case PROTO_PERMISSION:
        return parse_permission(root, &out->u.permission);
    case PROTO_DISMISS:
        return parse_dismiss(root, &out->u.dismiss);
    case PROTO_MODE:
        return parse_mode(root, &out->u.mode);
    case PROTO_IDENTIFY:
        return parse_identify(root, &out->u.identify_ms);
    }
    return false;
}

bool proto_parse(const char *json, size_t len, proto_msg_t *out)
{
    if (json == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return false;
    }
    bool ok = false;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsObject(root) && cJSON_IsString(type) && type->valuestring != NULL) {
        out->type = PROTO_UNKNOWN;
        for (size_t i = PROTO_UNKNOWN + 1; i < TYPE_COUNT; i++) {
            if (strcmp(type->valuestring, TYPE_NAMES[i]) == 0) {
                out->type = (proto_type_t)i;
                break;
            }
        }
        ok = parse_body(root, out);
    }
    cJSON_Delete(root);
    return ok;
}

/* ---- serialising ---- */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} writer_t;

static void put_raw(writer_t *w, const char *s, size_t n)
{
    if (w->overflow || w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
}

static void put(writer_t *w, const char *s)
{
    put_raw(w, s, strlen(s));
}

__attribute__((format(printf, 2, 3))) static void put_fmt(writer_t *w, const char *fmt, ...)
{
    char tmp[24];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        w->overflow = true;
        return;
    }
    put_raw(w, tmp, (size_t)n);
}

static void put_str(writer_t *w, const char *s)
{
    put(w, "\"");
    for (const unsigned char *p = (const unsigned char *)(s != NULL ? s : ""); *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            const char esc[2] = {'\\', (char)*p};
            put_raw(w, esc, 2);
        } else if (*p < 0x20) {
            put_fmt(w, "\\u%04x", (unsigned)*p);
        } else {
            put_raw(w, (const char *)p, 1);
        }
    }
    put(w, "\"");
}

static writer_t begin(char *buf, size_t cap)
{
    const writer_t w = {.buf = buf, .cap = cap, .len = 0, .overflow = buf == NULL || cap == 0};
    return w;
}

static int finish(writer_t *w)
{
    if (w->overflow) {
        if (w->buf != NULL && w->cap > 0) {
            w->buf[0] = '\0';
        }
        return -1;
    }
    w->buf[w->len] = '\0';
    return (int)w->len;
}

int proto_write_register(char *buf, size_t cap, const char *device_id, const char *model, const char *version)
{
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"register\",\"role\":\"consumer\",\"proto\":");
    put_fmt(&w, "%d", PROTO_VERSION);
    put(&w, ",\"client\":{\"kind\":\"device\",\"deviceId\":");
    put_str(&w, device_id);
    put(&w, ",\"model\":");
    put_str(&w, model);
    put(&w, ",\"version\":");
    put_str(&w, version);
    put(&w, "}}");
    return finish(&w);
}

int proto_write_pong(char *buf, size_t cap)
{
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"pong\"}");
    return finish(&w);
}

int proto_write_holding(char *buf, size_t cap, const char ids[][PROTO_ID_SIZE], size_t count)
{
    if (ids == NULL || count == 0 || count > PROTO_HOLDING_MAX_IDS) {
        return -1;
    }
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"holding\",\"ids\":[");
    for (size_t i = 0; i < count; i++) {
        if (!proto_id_is_valid(ids[i])) {
            w.overflow = true;
        }
        if (i > 0) {
            put(&w, ",");
        }
        put_str(&w, ids[i]);
    }
    put(&w, "]}");
    return finish(&w);
}

int proto_write_permission_response(char *buf, size_t cap, const char *id, bool granted)
{
    if (!proto_id_is_valid(id)) {
        return -1;
    }
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"permission_response\",\"id\":");
    put_str(&w, id);
    put(&w, granted ? ",\"granted\":true}" : ",\"granted\":false}");
    return finish(&w);
}

int proto_write_permission_defer(char *buf, size_t cap, const char *id)
{
    if (!proto_id_is_valid(id)) {
        return -1;
    }
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"permission_defer\",\"id\":");
    put_str(&w, id);
    put(&w, "}");
    return finish(&w);
}

int proto_write_set_mode(char *buf, size_t cap, proto_mode_t mode)
{
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"set_mode\",\"mode\":");
    put_str(&w, proto_mode_name(mode));
    put(&w, "}");
    return finish(&w);
}

int proto_write_telemetry(char *buf, size_t cap, const proto_telemetry_t *t)
{
    if (t == NULL || t->battery_state == NULL || t->reset_reason == NULL || t->fw == NULL) {
        return -1;
    }
    writer_t w = begin(buf, cap);
    put(&w, "{\"type\":\"telemetry\",\"battery\":{\"state\":");
    put_str(&w, t->battery_state);
    if (t->pct < 0) {
        put(&w, ",\"pct\":null");
    } else {
        put_fmt(&w, ",\"pct\":%d", t->pct > 100 ? 100 : t->pct);
    }
    put_fmt(&w, ",\"mv\":%d", t->mv);
    put(&w, t->ext_power ? "},\"power\":\"usb\"" : "},\"power\":\"battery\"");
    put_fmt(&w, ",\"rssi\":%d", t->rssi);
    put_fmt(&w, ",\"uptimeS\":%lu", (unsigned long)t->uptime_s);
    put_fmt(&w, ",\"heapFree\":%lu", (unsigned long)t->heap_free);
    put_fmt(&w, ",\"heapMin\":%lu", (unsigned long)t->heap_min);
    put(&w, ",\"resetReason\":");
    put_str(&w, t->reset_reason);
    put(&w, ",\"fw\":");
    put_str(&w, t->fw);
    if (t->screen != NULL) {
        put(&w, ",\"screen\":");
        put_str(&w, t->screen);
    }
    if (t->stage != NULL) {
        put(&w, ",\"stage\":");
        put_str(&w, t->stage);
    }
    if (t->alert != NULL) {
        put(&w, ",\"alert\":");
        put_str(&w, t->alert);
    }
    put(&w, "}");
    if (w.len > PROTO_TELEMETRY_MAX) {
        w.overflow = true;
    }
    return finish(&w);
}
