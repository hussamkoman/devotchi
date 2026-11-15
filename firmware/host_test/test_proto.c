#include "proto.h"

#include "cJSON.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#define ID  "9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e"

/* Static: proto_msg_t is too large for the stack frames the firmware allows itself. */
static proto_msg_t m;
static char frame[PROTO_MAX_FRAME + 64];

void setUp(void)
{
    memset(&m, 0xAA, sizeof(m));
}

void tearDown(void) {}

static bool parse(const char *json)
{
    return proto_parse(json, strlen(json), &m);
}

/* ---- inbound: the examples of docs/protocol.md ---- */

static void test_register_ack(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"role\":\"consumer\",\"roomId\":\"5e140e256d56\",\"proto\":2,"
                           "\"serverVersion\":\"0.2.0\",\"serverTime\":1794668400000,\"mode\":\"ask\","
                           "\"peers\":{\"producers\":1,\"consumers\":2,\"devices\":1},"
                           "\"device\":{\"id\":\"dv-e356e4\",\"name\":\"desk\"}}"));
    TEST_ASSERT_EQUAL(PROTO_REGISTER_ACK, m.type);
    TEST_ASSERT_EQUAL_INT32(2, m.u.register_ack.proto);
    TEST_ASSERT_EQUAL(PROTO_MODE_ASK, m.u.register_ack.mode);
    TEST_ASSERT_EQUAL_INT64(1794668400000LL, m.u.register_ack.server_time_ms);
    TEST_ASSERT_EQUAL_STRING("5e140e256d56", m.u.register_ack.room);
    TEST_ASSERT_EQUAL_STRING("dv-e356e4", m.u.register_ack.device_id);
    TEST_ASSERT_EQUAL_STRING("desk", m.u.register_ack.name);
    TEST_ASSERT_EQUAL_UINT16(1, m.u.register_ack.peers.producers);
    TEST_ASSERT_EQUAL_UINT16(2, m.u.register_ack.peers.consumers);
    TEST_ASSERT_EQUAL_UINT16(1, m.u.register_ack.peers.devices);
}

static void test_time(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2,\"serverTime\":1794668400000,\"utcOffsetMin\":60}"));
    TEST_ASSERT_TRUE(m.u.register_ack.has_offset);
    TEST_ASSERT_EQUAL_INT16(60, m.u.register_ack.utc_offset_min);
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2,\"utcOffsetMin\":900}"));
    TEST_ASSERT_FALSE(m.u.register_ack.has_offset);
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2}"));
    TEST_ASSERT_FALSE(m.u.register_ack.has_offset);

    TEST_ASSERT_TRUE(parse("{\"type\":\"time\",\"serverTime\":1794668400000,\"utcOffsetMin\":-720}"));
    TEST_ASSERT_EQUAL(PROTO_TIME, m.type);
    TEST_ASSERT_EQUAL_INT64(1794668400000LL, m.u.time.server_time_ms);
    TEST_ASSERT_EQUAL_INT16(-720, m.u.time.utc_offset_min);
    TEST_ASSERT_FALSE(parse("{\"type\":\"time\",\"serverTime\":1794668400000}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"time\",\"serverTime\":1794668400000,\"utcOffsetMin\":841}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"time\",\"serverTime\":\"x\",\"utcOffsetMin\":0}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"time\",\"serverTime\":0,\"utcOffsetMin\":0}"));
}

static void test_register_ack_variants(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2,\"mode\":\"desk\"}"));
    TEST_ASSERT_EQUAL(PROTO_MODE_DESK, m.u.register_ack.mode);
    TEST_ASSERT_EQUAL_STRING("", m.u.register_ack.name);
    TEST_ASSERT_EQUAL_INT64(0, m.u.register_ack.server_time_ms);

    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":1}"));
    TEST_ASSERT_EQUAL_INT32(1, m.u.register_ack.proto);

    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\",\"proto\":\"2\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\",\"proto\":2,\"device\":\"desk\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\",\"proto\":2,\"device\":{\"name\":5}}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\",\"proto\":2,\"peers\":[1,2,3]}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\",\"proto\":2,\"mode\":true}"));
}

static void test_ping_pong(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"ping\"}"));
    TEST_ASSERT_EQUAL(PROTO_PING, m.type);
    TEST_ASSERT_TRUE(parse("{\"type\":\"pong\"}"));
    TEST_ASSERT_EQUAL(PROTO_PONG, m.type);
    TEST_ASSERT_TRUE(parse(" { \"timestamp\" : \"2026-11-14T14:30:00.000Z\", \"type\" : \"ping\" } "));
    TEST_ASSERT_EQUAL(PROTO_PING, m.type);
}

static void test_error_without_and_with_id(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"error\",\"code\":\"INVALID_MESSAGE\",\"message\":\"status.msg must be a string\"}"));
    TEST_ASSERT_EQUAL(PROTO_ERROR, m.type);
    TEST_ASSERT_EQUAL_STRING("INVALID_MESSAGE", m.u.error.code);
    TEST_ASSERT_EQUAL_STRING("status.msg must be a string", m.u.error.message);
    TEST_ASSERT_FALSE(m.u.error.has_id);
    TEST_ASSERT_EQUAL_STRING("", m.u.error.id);

    TEST_ASSERT_TRUE(parse("{\"type\":\"error\",\"code\":\"UNKNOWN_REQUEST\",\"message\":\"not pending\",\"id\":\"" ID "\"}"));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN_REQUEST", m.u.error.code);
    TEST_ASSERT_TRUE(m.u.error.has_id);
    TEST_ASSERT_EQUAL_STRING(ID, m.u.error.id);

    TEST_ASSERT_TRUE(parse("{\"type\":\"error\",\"code\":\"UNSUPPORTED_PROTO\"}"));
    TEST_ASSERT_EQUAL_STRING("", m.u.error.message);

    TEST_ASSERT_FALSE(parse("{\"type\":\"error\",\"message\":\"no code\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"error\",\"code\":7}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"error\",\"code\":\"X\",\"id\":\"short\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"error\",\"code\":\"X\",\"id\":12}"));
}

static void test_peers(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"peers\",\"producers\":1,\"consumers\":2,\"devices\":1}"));
    TEST_ASSERT_EQUAL(PROTO_PEERS, m.type);
    TEST_ASSERT_EQUAL_UINT16(1, m.u.peers.producers);
    TEST_ASSERT_EQUAL_UINT16(2, m.u.peers.consumers);
    TEST_ASSERT_EQUAL_UINT16(1, m.u.peers.devices);
    TEST_ASSERT_TRUE(parse("{\"type\":\"peers\",\"producers\":-4,\"consumers\":1e9}"));
    TEST_ASSERT_EQUAL_UINT16(0, m.u.peers.producers);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, m.u.peers.consumers);
    TEST_ASSERT_FALSE(parse("{\"type\":\"peers\",\"producers\":\"1\"}"));
}

static void test_status(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"status\",\"msg\":\"Edit src/app.c\",\"mood\":\"working\",\"sid\":\"3fa9c1\","
                           "\"project\":\"devotchi\",\"timestamp\":\"2026-11-14T14:30:00.000Z\"}"));
    TEST_ASSERT_EQUAL(PROTO_STATUS, m.type);
    TEST_ASSERT_EQUAL_STRING("Edit src/app.c", m.u.status.msg);
    TEST_ASSERT_TRUE(m.u.status.has_mood);
    TEST_ASSERT_EQUAL(MOOD_WORKING, m.u.status.mood);
    TEST_ASSERT_EQUAL_STRING("3fa9c1", m.u.status.sid);
    TEST_ASSERT_EQUAL_STRING("devotchi", m.u.status.project);
}

static void test_status_without_mood_keeps_the_mood(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"status\",\"msg\":\"Reading files\"}"));
    TEST_ASSERT_FALSE(m.u.status.has_mood);
    TEST_ASSERT_EQUAL_STRING("", m.u.status.sid);
    TEST_ASSERT_EQUAL_STRING("", m.u.status.project);
}

static void test_status_whose_text_names_another_type(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"status\",\"msg\":\"ping\"}"));
    TEST_ASSERT_EQUAL(PROTO_STATUS, m.type);
    TEST_ASSERT_EQUAL_STRING("ping", m.u.status.msg);

    TEST_ASSERT_TRUE(parse("{\"msg\":\"{\\\"type\\\":\\\"permission\\\"}\",\"type\":\"status\"}"));
    TEST_ASSERT_EQUAL(PROTO_STATUS, m.type);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"permission\"}", m.u.status.msg);
}

static void test_status_invalid(void)
{
    TEST_ASSERT_FALSE(parse("{\"type\":\"status\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"status\",\"msg\":5}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"status\",\"msg\":null}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"status\",\"msg\":\"x\",\"mood\":3}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"status\",\"msg\":\"x\",\"sid\":123456}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"status\",\"msg\":\"x\",\"project\":[\"a\"]}"));
}

static void test_notify(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"notify\",\"event\":\"tests_failed\",\"mood\":\"sad\",\"detail\":\"3 of 47 failing\","
                           "\"urgency\":2,\"source\":\"ci\",\"project\":\"devotchi\","
                           "\"timestamp\":\"2026-11-14T14:30:00.000Z\"}"));
    TEST_ASSERT_EQUAL(PROTO_NOTIFY, m.type);
    TEST_ASSERT_EQUAL_STRING("tests_failed", m.u.notify.event);
    TEST_ASSERT_EQUAL(MOOD_SAD, m.u.notify.mood);
    TEST_ASSERT_EQUAL_STRING("3 of 47 failing", m.u.notify.detail);
    TEST_ASSERT_EQUAL_UINT8(2, m.u.notify.urgency);
    TEST_ASSERT_EQUAL_STRING("ci", m.u.notify.source);
    TEST_ASSERT_EQUAL_STRING("devotchi", m.u.notify.project);

    TEST_ASSERT_TRUE(parse("{\"type\":\"notify\",\"event\":\"task_complete\",\"mood\":\"happy\"}"));
    TEST_ASSERT_EQUAL_UINT8(1, m.u.notify.urgency);
    TEST_ASSERT_EQUAL_STRING("", m.u.notify.detail);

    TEST_ASSERT_TRUE(parse("{\"type\":\"notify\",\"event\":\"x\",\"mood\":\"happy\",\"urgency\":9}"));
    TEST_ASSERT_EQUAL_UINT8(3, m.u.notify.urgency);
    TEST_ASSERT_TRUE(parse("{\"type\":\"notify\",\"event\":\"x\",\"mood\":\"happy\",\"urgency\":-1}"));
    TEST_ASSERT_EQUAL_UINT8(0, m.u.notify.urgency);

    TEST_ASSERT_FALSE(parse("{\"type\":\"notify\",\"mood\":\"happy\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"notify\",\"event\":\"x\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"notify\",\"event\":\"x\",\"mood\":\"happy\",\"urgency\":\"2\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"notify\",\"event\":\"x\",\"mood\":\"happy\",\"detail\":false}"));
}

static void test_mood(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"mood\",\"mood\":\"anxious\",\"reason\":\"Long build running\",\"timestamp\":\"...\"}"));
    TEST_ASSERT_EQUAL(PROTO_MOOD, m.type);
    TEST_ASSERT_EQUAL(MOOD_ANXIOUS, m.u.mood.mood);
    TEST_ASSERT_EQUAL_STRING("Long build running", m.u.mood.reason);

    TEST_ASSERT_TRUE(parse("{\"type\":\"mood\",\"mood\":\"sleeping\"}"));
    TEST_ASSERT_EQUAL(MOOD_SLEEPING, m.u.mood.mood);
    TEST_ASSERT_EQUAL_STRING("", m.u.mood.reason);

    TEST_ASSERT_FALSE(parse("{\"type\":\"mood\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"mood\",\"mood\":1}"));
}

static void test_all_moods_and_unknown(void)
{
    static const char *const NAMES[MOOD_COUNT] = {"happy", "sad",  "excited", "anxious", "sleeping", "angry",
                                                  "proud", "idle", "focused", "working", "thinking"};
    for (int i = 0; i < MOOD_COUNT; i++) {
        char json[64];
        snprintf(json, sizeof(json), "{\"type\":\"mood\",\"mood\":\"%s\"}", NAMES[i]);
        TEST_ASSERT_TRUE(parse(json));
        TEST_ASSERT_EQUAL(i, m.u.mood.mood);
        TEST_ASSERT_EQUAL_STRING(NAMES[i], proto_mood_name((proto_mood_t)i));
    }
    TEST_ASSERT_TRUE(parse("{\"type\":\"mood\",\"mood\":\"ecstatic\"}"));
    TEST_ASSERT_EQUAL(MOOD_IDLE, m.u.mood.mood);
    TEST_ASSERT_TRUE(parse("{\"type\":\"mood\",\"mood\":\"Happy\"}"));
    TEST_ASSERT_EQUAL(MOOD_IDLE, m.u.mood.mood);
    TEST_ASSERT_EQUAL(MOOD_IDLE, proto_mood_from_name(NULL));
}

static void test_permission(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":\"Bash\","
                           "\"summary\":\"rm -rf dist\",\"details\":\"Remove build output\\n$ rm -rf dist\","
                           "\"risk\":\"high\",\"sid\":\"3fa9c1\",\"project\":\"devotchi\",\"timeout\":30000,"
                           "\"timestamp\":\"2026-11-14T14:30:00.000Z\"}"));
    TEST_ASSERT_EQUAL(PROTO_PERMISSION, m.type);
    TEST_ASSERT_EQUAL_STRING(ID, m.u.permission.id);
    TEST_ASSERT_EQUAL_STRING("Bash", m.u.permission.tool);
    TEST_ASSERT_EQUAL_STRING("rm -rf dist", m.u.permission.summary);
    TEST_ASSERT_EQUAL_STRING("Remove build output\n$ rm -rf dist", m.u.permission.details);
    TEST_ASSERT_EQUAL(PROTO_RISK_HIGH, m.u.permission.risk);
    TEST_ASSERT_EQUAL_STRING("3fa9c1", m.u.permission.sid);
    TEST_ASSERT_EQUAL_STRING("devotchi", m.u.permission.project);
    TEST_ASSERT_EQUAL_UINT32(30000, m.u.permission.timeout_ms);
}

#define PERM_HEAD  "{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":\"Edit\",\"summary\":\"s\""

static void test_permission_risk_and_timeout(void)
{
    TEST_ASSERT_TRUE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":1000}"));
    TEST_ASSERT_EQUAL(PROTO_RISK_LOW, m.u.permission.risk);
    TEST_ASSERT_EQUAL_STRING("", m.u.permission.details);
    TEST_ASSERT_TRUE(parse(PERM_HEAD ",\"risk\":\"med\",\"timeout\":1}"));
    TEST_ASSERT_EQUAL(PROTO_RISK_MED, m.u.permission.risk);
    TEST_ASSERT_EQUAL_UINT32(1, m.u.permission.timeout_ms);
    TEST_ASSERT_TRUE(parse(PERM_HEAD ",\"risk\":\"extreme\",\"timeout\":1500.7}"));
    TEST_ASSERT_EQUAL(PROTO_RISK_MED, m.u.permission.risk);
    TEST_ASSERT_EQUAL_UINT32(1500, m.u.permission.timeout_ms);
    TEST_ASSERT_TRUE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":1e12}"));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, m.u.permission.timeout_ms);

    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":0}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":0.5}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":-30000}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":\"30000\"}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":\"low\"}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"timeout\":30000}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":2,\"timeout\":30000}"));
    TEST_ASSERT_FALSE(parse(PERM_HEAD ",\"risk\":\"low\",\"timeout\":30000,\"details\":7}"));
}

static void test_permission_required_fields_and_id(void)
{
    static const char *const BAD[] = {
        "{\"type\":\"permission\",\"tool\":\"Edit\",\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"" ID "\",\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":\"Edit\",\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":5,\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":\"Edit\",\"summary\":{},\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"9f2c\",\"tool\":\"Edit\",\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"" ID "0\",\"tool\":\"Edit\",\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"9F2C4E6A8B0D1F3A5C7E9B1D3F5A7C9E\",\"tool\":\"Edit\",\"summary\":\"s\","
        "\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":\"9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9g\",\"tool\":\"Edit\",\"summary\":\"s\","
        "\"risk\":\"low\",\"timeout\":30000}",
        "{\"type\":\"permission\",\"id\":1234,\"tool\":\"Edit\",\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000}",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(parse(BAD[i]), BAD[i]);
    }
}

static void test_dismiss(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"outcome\":\"allow\","
                           "\"by\":\"dv-e356e4\"}"));
    TEST_ASSERT_EQUAL(PROTO_DISMISS, m.type);
    TEST_ASSERT_EQUAL_STRING(ID, m.u.dismiss.id);
    TEST_ASSERT_EQUAL(PROTO_DISMISS_ANSWERED, m.u.dismiss.reason);
    TEST_ASSERT_EQUAL(PROTO_OUTCOME_ALLOW, m.u.dismiss.outcome);
    TEST_ASSERT_EQUAL_STRING("dv-e356e4", m.u.dismiss.by);

    TEST_ASSERT_TRUE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"outcome\":\"deny\","
                           "\"by\":\"dashboard\"}"));
    TEST_ASSERT_EQUAL(PROTO_OUTCOME_DENY, m.u.dismiss.outcome);

    static const struct {
        const char *name;
        proto_dismiss_reason_t reason;
    } REASONS[] = {
        {"cancelled", PROTO_DISMISS_CANCELLED},
        {"timeout", PROTO_DISMISS_TIMEOUT},
        {"released", PROTO_DISMISS_RELEASED},
        {"exploded", PROTO_DISMISS_RELEASED},
    };
    for (size_t i = 0; i < sizeof(REASONS) / sizeof(REASONS[0]); i++) {
        char json[128];
        snprintf(json, sizeof(json), "{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"%s\"}", REASONS[i].name);
        TEST_ASSERT_TRUE(parse(json));
        TEST_ASSERT_EQUAL(REASONS[i].reason, m.u.dismiss.reason);
        TEST_ASSERT_EQUAL(PROTO_OUTCOME_NONE, m.u.dismiss.outcome);
        TEST_ASSERT_EQUAL_STRING("", m.u.dismiss.by);
    }

    TEST_ASSERT_FALSE(parse("{\"type\":\"dismiss\",\"reason\":\"timeout\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":4}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"outcome\":true}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"dismiss\",\"id\":\"nope\",\"reason\":\"answered\"}"));
}

static void test_mode(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"mode\",\"mode\":\"desk\"}"));
    TEST_ASSERT_EQUAL(PROTO_MODE, m.type);
    TEST_ASSERT_EQUAL(PROTO_MODE_DESK, m.u.mode);
    TEST_ASSERT_TRUE(parse("{\"type\":\"mode\",\"mode\":\"ask\"}"));
    TEST_ASSERT_EQUAL(PROTO_MODE_ASK, m.u.mode);
    TEST_ASSERT_FALSE(parse("{\"type\":\"mode\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"mode\",\"mode\":\"auto\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"mode\",\"mode\":1}"));
}

static void test_identify(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"identify\",\"durationMs\":3000}"));
    TEST_ASSERT_EQUAL(PROTO_IDENTIFY, m.type);
    TEST_ASSERT_EQUAL_UINT32(3000, m.u.identify_ms);
    TEST_ASSERT_TRUE(parse("{\"type\":\"identify\"}"));
    TEST_ASSERT_EQUAL_UINT32(PROTO_IDENTIFY_DEFAULT_MS, m.u.identify_ms);
    TEST_ASSERT_TRUE(parse("{\"type\":\"identify\",\"durationMs\":5}"));
    TEST_ASSERT_EQUAL_UINT32(PROTO_IDENTIFY_MIN_MS, m.u.identify_ms);
    TEST_ASSERT_TRUE(parse("{\"type\":\"identify\",\"durationMs\":999999}"));
    TEST_ASSERT_EQUAL_UINT32(PROTO_IDENTIFY_MAX_MS, m.u.identify_ms);
    TEST_ASSERT_FALSE(parse("{\"type\":\"identify\",\"durationMs\":\"3000\"}"));
}

/* ---- inbound: robustness ---- */

static void test_unknown_type_is_not_an_error(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"hologram\",\"data\":[1,2,{\"a\":null}]}"));
    TEST_ASSERT_EQUAL(PROTO_UNKNOWN, m.type);
    /* Consumer-to-server types are not something the device accepts. */
    TEST_ASSERT_TRUE(parse("{\"type\":\"permission_response\",\"id\":\"" ID "\",\"granted\":true}"));
    TEST_ASSERT_EQUAL(PROTO_UNKNOWN, m.type);
    TEST_ASSERT_TRUE(parse("{\"type\":\"\"}"));
    TEST_ASSERT_EQUAL(PROTO_UNKNOWN, m.type);
    TEST_ASSERT_TRUE(parse("{\"type\":\"PING\"}"));
    TEST_ASSERT_EQUAL(PROTO_UNKNOWN, m.type);
    TEST_ASSERT_TRUE(parse("{\"type\":\"unknown\"}"));
    TEST_ASSERT_EQUAL(PROTO_UNKNOWN, m.type);
}

static void test_unknown_fields_are_ignored(void)
{
    TEST_ASSERT_TRUE(parse("{\"future\":{\"deep\":[1,2,3]},\"type\":\"status\",\"msg\":\"hi\",\"action\":\"x\","
                           "\"extra\":null,\"n\":1.5}"));
    TEST_ASSERT_EQUAL(PROTO_STATUS, m.type);
    TEST_ASSERT_EQUAL_STRING("hi", m.u.status.msg);
}

static void test_not_a_message(void)
{
    static const char *const BAD[] = {
        "",
        "ping",
        "{",
        "{\"type\":\"ping\"",
        "[]",
        "[{\"type\":\"ping\"}]",
        "\"ping\"",
        "null",
        "42",
        "{}",
        "{\"msg\":\"no type\"}",
        "{\"type\":5}",
        "{\"type\":null}",
        "{\"type\":[\"ping\"]}",
        "{\"Type\":\"ping\"}",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(parse(BAD[i]), BAD[i]);
    }
    TEST_ASSERT_FALSE(proto_parse(NULL, 0, &m));
    TEST_ASSERT_FALSE(proto_parse("{\"type\":\"ping\"}", 15, NULL));
}

static void test_length_is_respected(void)
{
    /* The receive buffer is not NUL-terminated; bytes after len must not be read. */
    const char buf[] = "{\"type\":\"ping\"}garbage";
    TEST_ASSERT_TRUE(proto_parse(buf, 15, &m));
    TEST_ASSERT_EQUAL(PROTO_PING, m.type);
    TEST_ASSERT_FALSE(proto_parse(buf, 14, &m));
}

static void test_embedded_escapes(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"status\",\"msg\":\"say \\\"hi\\\" \\\\ tab\\there \\u0041\\n\\/end\"}"));
    TEST_ASSERT_EQUAL_STRING("say \"hi\" \\ tab\there A\n/end", m.u.status.msg);
    TEST_ASSERT_TRUE(parse("{\"type\":\"status\",\"msg\":\"caf\\u00e9 \\ud83d\\ude00\"}"));
    TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9 \xf0\x9f\x98\x80", m.u.status.msg);
}

static void test_oversize_strings_are_truncated(void)
{
    char *p = frame;
    p += sprintf(p, "{\"type\":\"permission\",\"id\":\"" ID "\",\"risk\":\"low\",\"timeout\":5000,\"tool\":\"");
    memset(p, 't', 60);
    p += 60;
    p += sprintf(p, "\",\"summary\":\"");
    memset(p, 's', 300);
    p += 300;
    p += sprintf(p, "\",\"details\":\"");
    memset(p, 'd', 900);
    p += 900;
    p += sprintf(p, "\",\"sid\":\"0123456789abcdef\",\"project\":\"");
    memset(p, 'p', 50);
    p += 50;
    sprintf(p, "\"}");

    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_EQUAL_size_t(PROTO_TOOL_SIZE - 1, strlen(m.u.permission.tool));
    TEST_ASSERT_EQUAL_size_t(PROTO_SUMMARY_SIZE - 1, strlen(m.u.permission.summary));
    TEST_ASSERT_EQUAL_size_t(PROTO_DETAILS_SIZE - 1, strlen(m.u.permission.details));
    TEST_ASSERT_EQUAL_STRING("01234567", m.u.permission.sid);
    TEST_ASSERT_EQUAL_size_t(PROTO_PROJECT_SIZE - 1, strlen(m.u.permission.project));
    TEST_ASSERT_EQUAL_UINT32(5000, m.u.permission.timeout_ms);
}

static void test_truncation_keeps_whole_code_points(void)
{
    /* 199 ASCII bytes followed by two-byte code points: the 200-byte limit falls inside the first one. */
    char *p = frame;
    p += sprintf(p, "{\"type\":\"status\",\"msg\":\"");
    memset(p, 'a', 199);
    p += 199;
    p += sprintf(p, "\xc3\xa9\xc3\xa9\"}");
    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_EQUAL_size_t(199, strlen(m.u.status.msg));

    p = frame;
    p += sprintf(p, "{\"type\":\"notify\",\"event\":\"e\",\"mood\":\"happy\",\"detail\":\"");
    memset(p, 'a', 298);
    p += 298;
    p += sprintf(p, "\xc3\xa9\xf0\x9f\x98\x80\"}");
    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_EQUAL_size_t(300, strlen(m.u.notify.detail));
    TEST_ASSERT_EQUAL_STRING("\xc3\xa9", m.u.notify.detail + 298);
}

static void test_largest_frame(void)
{
    char *p = frame;
    p += sprintf(p, "{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":\"Bash\",\"summary\":\"");
    memset(p, 's', 200);
    p += 200;
    p += sprintf(p, "\",\"risk\":\"high\",\"sid\":\"3fa9c1\",\"project\":\"devotchi\",\"timeout\":30000,\"details\":\"");
    const size_t tail = strlen("\"}");
    const size_t fill = PROTO_MAX_FRAME - (size_t)(p - frame) - tail;
    for (size_t i = 0; i < fill / 2; i++) {
        *p++ = '\\';
        *p++ = 'n';
    }
    if (fill % 2 != 0) {
        *p++ = 'x';
    }
    sprintf(p, "\"}");
    TEST_ASSERT_EQUAL_size_t(PROTO_MAX_FRAME, strlen(frame));

    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_EQUAL(PROTO_PERMISSION, m.type);
    TEST_ASSERT_EQUAL_size_t(200, strlen(m.u.permission.summary));
    TEST_ASSERT_EQUAL_size_t(PROTO_DETAILS_SIZE - 1, strlen(m.u.permission.details));
    TEST_ASSERT_EQUAL_CHAR('\n', m.u.permission.details[0]);
    TEST_ASSERT_EQUAL(PROTO_RISK_HIGH, m.u.permission.risk);
    TEST_ASSERT_EQUAL_UINT32(30000, m.u.permission.timeout_ms);
}

/* ---- outbound ---- */

static char out[512];

static cJSON *reparse(int len)
{
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_size_t((size_t)len, strlen(out));
    cJSON *root = cJSON_Parse(out);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, out);
    return root;
}

static const char *str_of(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(item), key);
    return item->valuestring;
}

static double num_of(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(item), key);
    return item->valuedouble;
}

static void test_write_register(void)
{
    const int n = proto_write_register(out, sizeof(out), "dv-e356e4", "devotchi-v1", "0.2.0", 0);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"register\",\"role\":\"consumer\",\"proto\":2,\"client\":{\"kind\":\"device\","
                             "\"deviceId\":\"dv-e356e4\",\"model\":\"devotchi-v1\",\"version\":\"0.2.0\"}}",
                             out);
    cJSON *root = reparse(n);
    const cJSON *client = cJSON_GetObjectItemCaseSensitive(root, "client");
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(client, "fw"));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "token"));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "roomId"));
    cJSON_Delete(root);
}

static void test_write_escapes_strings(void)
{
    const int n = proto_write_register(out, sizeof(out), "dv-e356e4", "mo\"del\\", "1.0\n\t\x01", 0);
    cJSON *root = reparse(n);
    const cJSON *client = cJSON_GetObjectItemCaseSensitive(root, "client");
    TEST_ASSERT_EQUAL_STRING("mo\"del\\", str_of(client, "model"));
    TEST_ASSERT_EQUAL_STRING("1.0\n\t\x01", str_of(client, "version"));
    cJSON_Delete(root);
}

static void test_write_simple_frames(void)
{
    TEST_ASSERT_EQUAL_INT(15, proto_write_pong(out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"pong\"}", out);

    TEST_ASSERT_GREATER_THAN(0, proto_write_permission_response(out, sizeof(out), ID, true));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"permission_response\",\"id\":\"" ID "\",\"granted\":true}", out);
    TEST_ASSERT_GREATER_THAN(0, proto_write_permission_response(out, sizeof(out), ID, false));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"permission_response\",\"id\":\"" ID "\",\"granted\":false}", out);

    TEST_ASSERT_GREATER_THAN(0, proto_write_permission_defer(out, sizeof(out), ID));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"permission_defer\",\"id\":\"" ID "\"}", out);

    TEST_ASSERT_GREATER_THAN(0, proto_write_set_mode(out, sizeof(out), PROTO_MODE_DESK));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"set_mode\",\"mode\":\"desk\"}", out);
    TEST_ASSERT_GREATER_THAN(0, proto_write_set_mode(out, sizeof(out), PROTO_MODE_ASK));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"set_mode\",\"mode\":\"ask\"}", out);
}

static void test_write_rejects_bad_ids(void)
{
    TEST_ASSERT_EQUAL_INT(-1, proto_write_permission_response(out, sizeof(out), "abc", true));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_permission_response(out, sizeof(out), NULL, true));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_permission_defer(out, sizeof(out), "\"},{\"x\":\"aaaaaaaaaaaaaaaaaaaaaaaaaa"));
    const char ids[1][PROTO_ID_SIZE] = {"zz"};
    TEST_ASSERT_EQUAL_INT(-1, proto_write_holding(out, sizeof(out), ids, 1));
}

static void test_write_holding(void)
{
    char ids[PROTO_HOLDING_MAX_IDS][PROTO_ID_SIZE];
    for (int i = 0; i < PROTO_HOLDING_MAX_IDS; i++) {
        snprintf(ids[i], sizeof(ids[i]), "%032x", i + 1);
    }
    TEST_ASSERT_GREATER_THAN(0, proto_write_holding(out, sizeof(out), ids, 1));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"holding\",\"ids\":[\"00000000000000000000000000000001\"]}", out);

    cJSON *root = reparse(proto_write_holding(out, sizeof(out), ids, PROTO_HOLDING_MAX_IDS));
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "ids");
    TEST_ASSERT_EQUAL_INT(PROTO_HOLDING_MAX_IDS, cJSON_GetArraySize(list));
    TEST_ASSERT_EQUAL_STRING(ids[7], cJSON_GetArrayItem(list, 7)->valuestring);
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_INT(-1, proto_write_holding(out, sizeof(out), ids, 0));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_holding(out, sizeof(out), ids, PROTO_HOLDING_MAX_IDS + 1));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_holding(out, sizeof(out), NULL, 1));
}

static void test_write_into_small_buffers(void)
{
    char small[16];
    TEST_ASSERT_EQUAL_INT(15, proto_write_pong(small, sizeof(small)));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_pong(small, 15));
    TEST_ASSERT_EQUAL_STRING("", small);
    TEST_ASSERT_EQUAL_INT(-1, proto_write_pong(small, 0));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_pong(NULL, 16));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_register(small, sizeof(small), "dv-e356e4", "devotchi-v1", "0.2.0", PROTO_CAP_DECIDE));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_permission_response(small, sizeof(small), ID, true));
}

static void test_write_telemetry(void)
{
    const proto_telemetry_t t = {
        .battery_state = "discharging",
        .pct = 82,
        .mv = 3987,
        .ext_power = false,
        .rssi = -58,
        .uptime_s = 5321,
        .heap_free = 148392,
        .heap_min = 101224,
        .reset_reason = "poweron",
        .fw = "0.2.0",
        .screen = "pet",
        .stage = "active",
        .alert = "battery_low",
    };
    const int n = proto_write_telemetry(out, sizeof(out), &t);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"telemetry\",\"battery\":{\"state\":\"discharging\",\"pct\":82,\"mv\":3987},"
                             "\"power\":\"battery\",\"rssi\":-58,\"uptimeS\":5321,\"heapFree\":148392,\"heapMin\":101224,"
                             "\"resetReason\":\"poweron\",\"fw\":\"0.2.0\",\"screen\":\"pet\",\"stage\":\"active\","
                             "\"alert\":\"battery_low\"}",
                             out);
    cJSON *root = reparse(n);
    TEST_ASSERT_EQUAL_INT(-58, (int)num_of(root, "rssi"));
    cJSON_Delete(root);
}

static void test_write_telemetry_absent_battery(void)
{
    const proto_telemetry_t t = {
        .battery_state = "absent",
        .pct = -1,
        .mv = 4122,
        .ext_power = true,
        .rssi = -40,
        .uptime_s = 12,
        .heap_free = 200000,
        .heap_min = 190000,
        .reset_reason = "usb",
        .fw = "0.2.0",
    };
    cJSON *root = reparse(proto_write_telemetry(out, sizeof(out), &t));
    const cJSON *battery = cJSON_GetObjectItemCaseSensitive(root, "battery");
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(battery, "pct")));
    TEST_ASSERT_EQUAL_STRING("absent", str_of(battery, "state"));
    TEST_ASSERT_EQUAL_STRING("usb", str_of(root, "power"));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "alert"));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "screen"));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "stage"));
    cJSON_Delete(root);
}

static void test_write_telemetry_limits(void)
{
    /* The widest values every field can take still fit the 300-byte limit. */
    proto_telemetry_t t = {
        .battery_state = "discharging",
        .pct = 100,
        .mv = 65535,
        .ext_power = false,
        .rssi = -127,
        .uptime_s = UINT32_MAX,
        .heap_free = UINT32_MAX,
        .heap_min = UINT32_MAX,
        .reset_reason = "deepsleep",
        .fw = "123456789012345678901234",
        .screen = "provisioning",
        .stage = "active",
        .alert = "battery_critical",
    };
    const int n = proto_write_telemetry(out, sizeof(out), &t);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_LESS_OR_EQUAL(PROTO_TELEMETRY_MAX, n);

    static char long_fw[200];
    memset(long_fw, 'v', sizeof(long_fw) - 1);
    t.fw = long_fw;
    TEST_ASSERT_EQUAL_INT(-1, proto_write_telemetry(out, sizeof(out), &t));

    t.fw = NULL;
    TEST_ASSERT_EQUAL_INT(-1, proto_write_telemetry(out, sizeof(out), &t));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_telemetry(out, sizeof(out), NULL));
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("register_ack", proto_type_name(PROTO_REGISTER_ACK));
    TEST_ASSERT_EQUAL_STRING("identify", proto_type_name(PROTO_IDENTIFY));
    TEST_ASSERT_EQUAL_STRING("unknown", proto_type_name(PROTO_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("unknown", proto_type_name((proto_type_t)99));
    TEST_ASSERT_EQUAL_STRING("ask", proto_mode_name(PROTO_MODE_ASK));
    TEST_ASSERT_EQUAL_STRING("desk", proto_mode_name(PROTO_MODE_DESK));
    TEST_ASSERT_TRUE(proto_id_is_valid(ID));
    TEST_ASSERT_FALSE(proto_id_is_valid(NULL));
}

/* ---- caps, card fields and decide (protocol 14) ---- */

#define CARD_HEAD  "{\"type\":\"permission\",\"id\":\"" ID "\",\"tool\":\"Bash\",\"summary\":\"s\",\"risk\":\"low\",\"timeout\":30000"

static void test_permission_card_fields(void)
{
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"pnpm test --filter server\\nline 2\",\"desc\":\"Run the tests\","
                                     "\"reason\":\"Force push\",\"grant\":{\"rule\":\"pnpm test\",\"match\":\"prefix\"}}"));
    const proto_permission_t *p = &m.u.permission;
    TEST_ASSERT_TRUE(p->has_cmd);
    TEST_ASSERT_EQUAL_STRING("pnpm test --filter server\nline 2", p->cmd);
    TEST_ASSERT_FALSE(p->cut);
    TEST_ASSERT_EQUAL_STRING("Run the tests", p->desc);
    TEST_ASSERT_EQUAL_STRING("Force push", p->reason);
    TEST_ASSERT_TRUE(p->has_grant);
    TEST_ASSERT_TRUE(p->grant_prefix);
    TEST_ASSERT_EQUAL_STRING("pnpm test", p->grant_rule);

    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"git status\",\"grant\":{\"rule\":\"git status\",\"match\":\"exact\"}}"));
    TEST_ASSERT_TRUE(m.u.permission.has_grant);
    TEST_ASSERT_FALSE(m.u.permission.grant_prefix);
}

static void test_permission_phase1_fallback(void)
{
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"details\":\"$ ls\"}"));
    TEST_ASSERT_FALSE(m.u.permission.has_cmd);
    TEST_ASSERT_EQUAL_STRING("", m.u.permission.cmd);
    TEST_ASSERT_EQUAL_STRING("$ ls", m.u.permission.details);
    TEST_ASSERT_FALSE(m.u.permission.cut);
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
    TEST_ASSERT_EQUAL_STRING("", m.u.permission.desc);
    TEST_ASSERT_EQUAL_STRING("", m.u.permission.reason);
}

static void test_permission_cut_drops_grant(void)
{
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"a\",\"cut\":true,\"grant\":{\"rule\":\"a\",\"match\":\"exact\"}}"));
    TEST_ASSERT_TRUE(m.u.permission.cut);
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
    TEST_ASSERT_EQUAL_STRING("", m.u.permission.grant_rule);
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"a\",\"cut\":false}"));
    TEST_ASSERT_FALSE(m.u.permission.cut);
}

static void test_permission_oversized_cmd_is_cut(void)
{
    static char json[2048];
    char cmd[PROTO_CMD_SIZE + 1];
    memset(cmd, 'x', sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    snprintf(json, sizeof(json), CARD_HEAD ",\"cmd\":\"%s\",\"grant\":{\"rule\":\"x\",\"match\":\"exact\"}}", cmd);
    TEST_ASSERT_TRUE(parse(json));
    TEST_ASSERT_EQUAL_size_t(PROTO_CMD_SIZE - 1, strlen(m.u.permission.cmd));
    TEST_ASSERT_TRUE(m.u.permission.cut);
    TEST_ASSERT_FALSE(m.u.permission.has_grant);

    /* Exactly 600 bytes still fits. */
    cmd[PROTO_CMD_SIZE - 1] = '\0';
    snprintf(json, sizeof(json), CARD_HEAD ",\"cmd\":\"%s\"}", cmd);
    TEST_ASSERT_TRUE(parse(json));
    TEST_ASSERT_FALSE(m.u.permission.cut);
}

static void test_permission_grant_not_shown_word_for_word(void)
{
    static char json[1024];
    char rule[PROTO_RULE_SIZE + 1];
    memset(rule, 'r', sizeof(rule) - 1);
    rule[sizeof(rule) - 1] = '\0';
    snprintf(json, sizeof(json), CARD_HEAD ",\"cmd\":\"r\",\"grant\":{\"rule\":\"%s\",\"match\":\"exact\"}}", rule);
    TEST_ASSERT_TRUE(parse(json));
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"r\",\"grant\":{\"rule\":\"caf\\u00e9\",\"match\":\"exact\"}}"));
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"r\",\"grant\":{\"rule\":\"r\",\"match\":\"glob\"}}"));
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"r\",\"grant\":{\"rule\":\"\",\"match\":\"exact\"}}"));
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
    TEST_ASSERT_TRUE(parse(CARD_HEAD ",\"cmd\":\"r\",\"grant\":{\"rule\":\"a\\nb\",\"match\":\"exact\"}}"));
    TEST_ASSERT_FALSE(m.u.permission.has_grant);
}

static void test_permission_card_wrong_types(void)
{
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"cmd\":5}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"cut\":\"true\"}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"cut\":1}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"desc\":[]}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"reason\":null}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"grant\":\"pnpm test\"}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"grant\":{\"rule\":1,\"match\":\"exact\"}}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"grant\":{\"match\":\"exact\"}}"));
    TEST_ASSERT_FALSE(parse(CARD_HEAD ",\"grant\":{\"rule\":\"a\"}}"));
}

static void test_permission_oversized_desc_and_reason_truncated(void)
{
    static char json[2048];
    char desc[PROTO_DESC_SIZE + 10];
    char reason[PROTO_RISK_REASON_SIZE + 10];
    memset(desc, 'd', sizeof(desc) - 1);
    desc[sizeof(desc) - 1] = '\0';
    memset(reason, 'w', sizeof(reason) - 1);
    reason[sizeof(reason) - 1] = '\0';
    snprintf(json, sizeof(json), CARD_HEAD ",\"cmd\":\"a\",\"desc\":\"%s\",\"reason\":\"%s\"}", desc, reason);
    TEST_ASSERT_TRUE(parse(json));
    TEST_ASSERT_EQUAL_size_t(PROTO_DESC_SIZE - 1, strlen(m.u.permission.desc));
    TEST_ASSERT_EQUAL_size_t(PROTO_RISK_REASON_SIZE - 1, strlen(m.u.permission.reason));
    TEST_ASSERT_FALSE(m.u.permission.cut);
}

static void test_register_ack_caps(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2,\"caps\":[\"decide\",\"ask\",\"future\",7,\"pets\"]}"));
    TEST_ASSERT_EQUAL_UINT32(PROTO_CAP_DECIDE | PROTO_CAP_ASK | PROTO_CAP_PETS, m.u.register_ack.caps);
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2}"));
    TEST_ASSERT_EQUAL_UINT32(0, m.u.register_ack.caps);
    TEST_ASSERT_TRUE(parse("{\"type\":\"register_ack\",\"proto\":2,\"caps\":[]}"));
    TEST_ASSERT_EQUAL_UINT32(0, m.u.register_ack.caps);
    TEST_ASSERT_FALSE(parse("{\"type\":\"register_ack\",\"proto\":2,\"caps\":\"decide\"}"));
}

static void test_dismiss_choice(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"outcome\":\"allow\","
                           "\"by\":\"dashboard\",\"choice\":\"allow_session\"}"));
    TEST_ASSERT_EQUAL(PROTO_CHOICE_ALLOW_SESSION, m.u.dismiss.choice);
    TEST_ASSERT_TRUE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"choice\":\"deny_stop\"}"));
    TEST_ASSERT_EQUAL(PROTO_CHOICE_DENY_STOP, m.u.dismiss.choice);
    TEST_ASSERT_TRUE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"choice\":\"later\"}"));
    TEST_ASSERT_EQUAL(PROTO_CHOICE_NONE, m.u.dismiss.choice);
    TEST_ASSERT_TRUE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\"}"));
    TEST_ASSERT_EQUAL(PROTO_CHOICE_NONE, m.u.dismiss.choice);
    TEST_ASSERT_FALSE(parse("{\"type\":\"dismiss\",\"id\":\"" ID "\",\"reason\":\"answered\",\"choice\":1}"));
}

static void test_error_not_allowed_with_id(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"error\",\"code\":\"NOT_ALLOWED\",\"message\":\"cut\",\"id\":\"" ID "\"}"));
    TEST_ASSERT_EQUAL_STRING("NOT_ALLOWED", m.u.error.code);
    TEST_ASSERT_TRUE(m.u.error.has_id);
    TEST_ASSERT_EQUAL_STRING(ID, m.u.error.id);
    TEST_ASSERT_FALSE(parse("{\"type\":\"error\",\"code\":\"NOT_ALLOWED\",\"id\":\"XYZ\"}"));
}

static char wout[256];

static void test_write_decide(void)
{
    TEST_ASSERT_GREATER_THAN(0, proto_write_decide(wout, sizeof(wout), ID, PROTO_CHOICE_DENY_ASK_WHY));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"decide\",\"id\":\"" ID "\",\"choice\":\"deny_ask_why\"}", wout);
    TEST_ASSERT_GREATER_THAN(0, proto_write_decide(wout, sizeof(wout), ID, PROTO_CHOICE_ALLOW_SESSION));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"decide\",\"id\":\"" ID "\",\"choice\":\"allow_session\"}", wout);
    TEST_ASSERT_GREATER_THAN(0, proto_write_decide(wout, sizeof(wout), ID, PROTO_CHOICE_DENY_STOP));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, sizeof(wout), ID, PROTO_CHOICE_NONE));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, sizeof(wout), ID, PROTO_CHOICE_ANSWERED));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, sizeof(wout), ID, PROTO_CHOICE_COUNT));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, sizeof(wout), "9F2C4E6A8B0D1F3A5C7E9B1D3F5A7C9E", PROTO_CHOICE_DENY_STOP));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, sizeof(wout), "9f2c", PROTO_CHOICE_DENY_STOP));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, sizeof(wout), NULL, PROTO_CHOICE_DENY_STOP));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_decide(wout, 20, ID, PROTO_CHOICE_DENY_STOP));
}

/* ---- questions, plans, deadline (protocol 14.4-14.6) ---- */

static void test_parse_question(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"question\",\"id\":\"" ID "\",\"index\":1,\"count\":2,\"header\":\"Framework\","
                           "\"text\":\"Which framework should the dashboard use?\",\"multi\":true,"
                           "\"options\":[{\"label\":\"React\",\"desc\":\"Component library\"},{\"label\":\"Vue\"}],"
                           "\"sid\":\"3fa9c1\",\"project\":\"devotchi\",\"timeout\":30000,"
                           "\"timestamp\":\"2026-11-15T09:00:00.000Z\"}"));
    TEST_ASSERT_EQUAL(PROTO_QUESTION, m.type);
    const proto_question_t *q = &m.u.question;
    TEST_ASSERT_EQUAL_STRING(ID, q->id);
    TEST_ASSERT_EQUAL_UINT8(1, q->index);
    TEST_ASSERT_EQUAL_UINT8(2, q->count);
    TEST_ASSERT_TRUE(q->has_header);
    TEST_ASSERT_EQUAL_STRING("Framework", q->header);
    TEST_ASSERT_TRUE(q->multi);
    TEST_ASSERT_EQUAL_UINT8(2, q->n_options);
    TEST_ASSERT_EQUAL_STRING("React", q->options[0].label);
    TEST_ASSERT_EQUAL_STRING("Component library", q->options[0].desc);
    TEST_ASSERT_EQUAL_STRING("", q->options[1].desc);
    TEST_ASSERT_EQUAL_UINT32(30000, q->timeout_ms);
    TEST_ASSERT_EQUAL_STRING("3fa9c1", q->sid);
}

static void test_parse_question_header_optional(void)
{
    static const char *const headers[] = {"", ",\"header\":\"\"", ",\"header\":42", ",\"header\":null"};
    for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
        snprintf(frame, sizeof(frame),
                 "{\"type\":\"question\",\"id\":\"" ID "\",\"index\":0,\"count\":1%s,\"text\":\"Q?\","
                 "\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}],\"timeout\":1000}",
                 headers[i]);
        TEST_ASSERT_TRUE_MESSAGE(parse(frame), headers[i]);
        TEST_ASSERT_FALSE(m.u.question.has_header);
        TEST_ASSERT_FALSE(m.u.question.multi);
    }
}

static void test_parse_question_rejects(void)
{
    static const char *const bad[] = {
        /* index past count, count 0 and 5, fractional index */
        "\"index\":1,\"count\":1,\"text\":\"Q\",\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]",
        "\"index\":0,\"count\":0,\"text\":\"Q\",\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]",
        "\"index\":0,\"count\":5,\"text\":\"Q\",\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]",
        "\"index\":0.5,\"count\":2,\"text\":\"Q\",\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]",
        /* one option, five options, empty label, a label that is not a string, no text */
        "\"index\":0,\"count\":1,\"text\":\"Q\",\"options\":[{\"label\":\"A\"}]",
        "\"index\":0,\"count\":1,\"text\":\"Q\",\"options\":[{\"label\":\"A\"},{\"label\":\"B\"},{\"label\":\"C\"},"
        "{\"label\":\"D\"},{\"label\":\"E\"}]",
        "\"index\":0,\"count\":1,\"text\":\"Q\",\"options\":[{\"label\":\"\"},{\"label\":\"B\"}]",
        "\"index\":0,\"count\":1,\"text\":\"Q\",\"options\":[{\"label\":1},{\"label\":\"B\"}]",
        "\"index\":0,\"count\":1,\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]",
        "\"index\":0,\"count\":1,\"text\":\"Q\",\"multi\":1,\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(frame, sizeof(frame), "{\"type\":\"question\",\"id\":\"" ID "\",%s,\"timeout\":1000}", bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(parse(frame), bad[i]);
    }
    TEST_ASSERT_FALSE(parse("{\"type\":\"question\",\"id\":\"" ID "\",\"index\":0,\"count\":1,\"text\":\"Q\","
                            "\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]}"));
}

static void test_parse_question_largest_frame(void)
{
    /* Four options with long labels and descriptions: the frame stays within 2048 bytes and every field fits. */
    char text[PROTO_Q_TEXT_SIZE];
    char label[PROTO_Q_LABEL_SIZE];
    char desc[PROTO_Q_DESC_SIZE];
    memset(text, 't', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    memset(label, 'l', sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    memset(desc, 'd', 170);
    desc[170] = '\0';
    const int n = snprintf(frame, sizeof(frame),
                           "{\"type\":\"question\",\"id\":\"" ID "\",\"index\":3,\"count\":4,\"text\":\"%s\","
                           "\"options\":[{\"label\":\"%s\",\"desc\":\"%s\"},{\"label\":\"%s\",\"desc\":\"%s\"},"
                           "{\"label\":\"%s\",\"desc\":\"%s\"},{\"label\":\"%s\",\"desc\":\"%s\"}],\"timeout\":1}",
                           text, label, desc, label, desc, label, desc, label, desc);
    TEST_ASSERT_LESS_OR_EQUAL(PROTO_MAX_FRAME, n);
    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_EQUAL_size_t(PROTO_Q_TEXT_SIZE - 1, strlen(m.u.question.text));
    TEST_ASSERT_EQUAL_size_t(PROTO_Q_LABEL_SIZE - 1, strlen(m.u.question.options[3].label));
    TEST_ASSERT_EQUAL_size_t(170, strlen(m.u.question.options[3].desc));
}

static void test_parse_plan(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"plan\",\"id\":\"" ID "\",\"title\":\"Add pet packs\","
                           "\"steps\":[\"Define the pack format\",\"Add the pets partition\",\"Port Devo\"],"
                           "\"more\":0,\"cut\":true,\"sid\":\"3fa9c1\",\"project\":\"devotchi\",\"timeout\":30000}"));
    TEST_ASSERT_EQUAL(PROTO_PLAN, m.type);
    TEST_ASSERT_EQUAL_STRING("Add pet packs", m.u.plan.title);
    TEST_ASSERT_EQUAL_UINT8(3, m.u.plan.n_steps);
    TEST_ASSERT_EQUAL_STRING("Port Devo", m.u.plan.steps[2]);
    TEST_ASSERT_TRUE(m.u.plan.cut);
    TEST_ASSERT_EQUAL_UINT16(0, m.u.plan.more);

    /* cut absent is false; more > 0 makes the frame cut on the device as well. */
    TEST_ASSERT_TRUE(parse("{\"type\":\"plan\",\"id\":\"" ID "\",\"title\":\"T\",\"steps\":[\"a\"],\"timeout\":1}"));
    TEST_ASSERT_FALSE(m.u.plan.cut);
    TEST_ASSERT_TRUE(parse("{\"type\":\"plan\",\"id\":\"" ID "\",\"title\":\"T\",\"steps\":[\"a\"],\"more\":4,"
                           "\"timeout\":1}"));
    TEST_ASSERT_TRUE(m.u.plan.cut);
    TEST_ASSERT_EQUAL_UINT16(4, m.u.plan.more);
}

static void test_parse_plan_overlong(void)
{
    /* Thirteen steps: the thirteenth counts as more; a step longer than its buffer makes the frame cut. */
    char steps[1024] = "";
    for (int i = 0; i < 13; i++) {
        char step[16];
        snprintf(step, sizeof(step), "%s\"s%02d\"", i > 0 ? "," : "", i);
        strcat(steps, step);
    }
    snprintf(frame, sizeof(frame), "{\"type\":\"plan\",\"id\":\"" ID "\",\"title\":\"T\",\"steps\":[%s],\"timeout\":1}",
             steps);
    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_EQUAL_UINT8(PROTO_PLAN_STEPS_MAX, m.u.plan.n_steps);
    TEST_ASSERT_EQUAL_UINT16(1, m.u.plan.more);
    TEST_ASSERT_TRUE(m.u.plan.cut);

    char longstep[PROTO_PLAN_STEP_SIZE + 2];
    memset(longstep, 'x', sizeof(longstep) - 1);
    longstep[sizeof(longstep) - 1] = '\0';
    snprintf(frame, sizeof(frame), "{\"type\":\"plan\",\"id\":\"" ID "\",\"title\":\"T\",\"steps\":[\"%s\"],\"timeout\":1}",
             longstep);
    TEST_ASSERT_TRUE(parse(frame));
    TEST_ASSERT_TRUE(m.u.plan.cut);
    TEST_ASSERT_EQUAL_size_t(PROTO_PLAN_STEP_SIZE - 1, strlen(m.u.plan.steps[0]));

    TEST_ASSERT_FALSE(parse("{\"type\":\"plan\",\"id\":\"" ID "\",\"steps\":[1],\"timeout\":1}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"plan\",\"id\":\"" ID "\",\"steps\":\"a\",\"timeout\":1}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"plan\",\"id\":\"" ID "\",\"steps\":[],\"timeout\":0}"));
}

static void test_parse_deadline(void)
{
    TEST_ASSERT_TRUE(parse("{\"type\":\"deadline\",\"id\":\"" ID "\",\"timeout\":60000}"));
    TEST_ASSERT_EQUAL(PROTO_DEADLINE, m.type);
    TEST_ASSERT_EQUAL_STRING(ID, m.u.deadline.id);
    TEST_ASSERT_EQUAL_UINT32(60000, m.u.deadline.timeout_ms);
    TEST_ASSERT_FALSE(parse("{\"type\":\"deadline\",\"id\":\"" ID "\"}"));
    TEST_ASSERT_FALSE(parse("{\"type\":\"deadline\",\"timeout\":1000}"));
    TEST_ASSERT_EQUAL_STRING("deadline", proto_type_name(PROTO_DEADLINE));
    TEST_ASSERT_EQUAL_STRING("question", proto_type_name(PROTO_QUESTION));
}

static void test_write_question_response(void)
{
    const proto_answer_t answers[] = {{.mask = 1u << 0}, {.mask = (1u << 1) | (1u << 3)}, {.mask = 0},
                                      {.no_preference = true, .mask = 0xF}};
    TEST_ASSERT_GREATER_THAN(0, proto_write_question_response(wout, sizeof(wout), ID, answers, 4));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"question_response\",\"id\":\"" ID "\",\"answers\":[[0],[1,3],[],null]}", wout);
    TEST_ASSERT_GREATER_THAN(0, proto_write_question_response(wout, sizeof(wout), ID, answers, 1));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"question_response\",\"id\":\"" ID "\",\"answers\":[[0]]}", wout);
    TEST_ASSERT_EQUAL_INT(-1, proto_write_question_response(wout, sizeof(wout), ID, answers, 0));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_question_response(wout, sizeof(wout), ID, answers, 5));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_question_response(wout, sizeof(wout), "x", answers, 1));
    const proto_answer_t bad = {.mask = 1u << 4};
    TEST_ASSERT_EQUAL_INT(-1, proto_write_question_response(wout, sizeof(wout), ID, &bad, 1));
    TEST_ASSERT_EQUAL_INT(-1, proto_write_question_response(wout, 40, ID, answers, 4));
}

static void test_write_extend_and_plan_choices(void)
{
    TEST_ASSERT_GREATER_THAN(0, proto_write_extend(wout, sizeof(wout), ID));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"extend\",\"id\":\"" ID "\"}", wout);
    TEST_ASSERT_EQUAL_INT(-1, proto_write_extend(wout, sizeof(wout), "nope"));
    static const char *const names[] = {"approve", "approve_accept_edits", "revise", "decline"};
    static const proto_choice_t choices[] = {PROTO_CHOICE_APPROVE, PROTO_CHOICE_APPROVE_ACCEPT_EDITS, PROTO_CHOICE_REVISE,
                                             PROTO_CHOICE_DECLINE};
    for (size_t i = 0; i < 4; i++) {
        char expected[96];
        snprintf(expected, sizeof(expected), "{\"type\":\"decide\",\"id\":\"" ID "\",\"choice\":\"%s\"}", names[i]);
        TEST_ASSERT_GREATER_THAN(0, proto_write_decide(wout, sizeof(wout), ID, choices[i]));
        TEST_ASSERT_EQUAL_STRING(expected, wout);
    }
}

static void test_write_register_caps(void)
{
    TEST_ASSERT_GREATER_THAN(0, proto_write_register(wout, sizeof(wout), "dv-e356e4", "m", "1", PROTO_CAP_DECIDE));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"register\",\"role\":\"consumer\",\"proto\":2,\"client\":{\"kind\":\"device\","
                             "\"deviceId\":\"dv-e356e4\",\"model\":\"m\",\"version\":\"1\",\"caps\":[\"decide\"]}}",
                             wout);
    TEST_ASSERT_GREATER_THAN(0, proto_write_register(wout, sizeof(wout), "d", "m", "1",
                                                     PROTO_CAP_DECIDE | PROTO_CAP_PLAN | PROTO_CAP_PETS));
    TEST_ASSERT_NOT_NULL(strstr(wout, "\"caps\":[\"decide\",\"plan\",\"pets\"]"));
}

static void test_choice_names(void)
{
    TEST_ASSERT_EQUAL_STRING("deny_ask_why", proto_choice_name(PROTO_CHOICE_DENY_ASK_WHY));
    TEST_ASSERT_EQUAL_STRING("", proto_choice_name(PROTO_CHOICE_NONE));
    TEST_ASSERT_EQUAL_STRING("", proto_choice_name(PROTO_CHOICE_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_register_ack);
    RUN_TEST(test_register_ack_variants);
    RUN_TEST(test_time);
    RUN_TEST(test_ping_pong);
    RUN_TEST(test_error_without_and_with_id);
    RUN_TEST(test_peers);
    RUN_TEST(test_status);
    RUN_TEST(test_status_without_mood_keeps_the_mood);
    RUN_TEST(test_status_whose_text_names_another_type);
    RUN_TEST(test_status_invalid);
    RUN_TEST(test_notify);
    RUN_TEST(test_mood);
    RUN_TEST(test_all_moods_and_unknown);
    RUN_TEST(test_permission);
    RUN_TEST(test_permission_risk_and_timeout);
    RUN_TEST(test_permission_required_fields_and_id);
    RUN_TEST(test_dismiss);
    RUN_TEST(test_mode);
    RUN_TEST(test_identify);
    RUN_TEST(test_unknown_type_is_not_an_error);
    RUN_TEST(test_unknown_fields_are_ignored);
    RUN_TEST(test_not_a_message);
    RUN_TEST(test_length_is_respected);
    RUN_TEST(test_embedded_escapes);
    RUN_TEST(test_oversize_strings_are_truncated);
    RUN_TEST(test_truncation_keeps_whole_code_points);
    RUN_TEST(test_largest_frame);
    RUN_TEST(test_write_register);
    RUN_TEST(test_write_escapes_strings);
    RUN_TEST(test_write_simple_frames);
    RUN_TEST(test_write_rejects_bad_ids);
    RUN_TEST(test_write_holding);
    RUN_TEST(test_write_into_small_buffers);
    RUN_TEST(test_write_telemetry);
    RUN_TEST(test_write_telemetry_absent_battery);
    RUN_TEST(test_write_telemetry_limits);
    RUN_TEST(test_names);
    RUN_TEST(test_permission_card_fields);
    RUN_TEST(test_permission_phase1_fallback);
    RUN_TEST(test_permission_cut_drops_grant);
    RUN_TEST(test_permission_oversized_cmd_is_cut);
    RUN_TEST(test_permission_grant_not_shown_word_for_word);
    RUN_TEST(test_permission_card_wrong_types);
    RUN_TEST(test_permission_oversized_desc_and_reason_truncated);
    RUN_TEST(test_register_ack_caps);
    RUN_TEST(test_dismiss_choice);
    RUN_TEST(test_error_not_allowed_with_id);
    RUN_TEST(test_write_decide);
    RUN_TEST(test_write_register_caps);
    RUN_TEST(test_parse_question);
    RUN_TEST(test_parse_question_header_optional);
    RUN_TEST(test_parse_question_rejects);
    RUN_TEST(test_parse_question_largest_frame);
    RUN_TEST(test_parse_plan);
    RUN_TEST(test_parse_plan_overlong);
    RUN_TEST(test_parse_deadline);
    RUN_TEST(test_write_question_response);
    RUN_TEST(test_write_extend_and_plan_choices);
    RUN_TEST(test_choice_names);
    return UNITY_END();
}
