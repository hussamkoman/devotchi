#include "app_priv.h"

#include "app.h"
#include "battery.h"
#include "btn_fsm.h"
#include "console.h"
#include "fb_seq.h"
#include "feedback.h"
#include "power.h"
#include "storage.h"

#include "esp_check.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_cmds_io";

#define INJECT_MV_MAX     5000
#define FB_DURATION_MAX   600000

typedef struct {
    const char *key;
    storage_setting_t id;
    const uint16_t *values;
    size_t count;
} setting_def_t;

static const uint16_t BRIGHTNESS_VALUES[] = {25, 50, 75, 100};
static const uint16_t DIM_S_VALUES[] = {15, 30, 60, 120};
static const uint16_t DEEP_MIN_VALUES[] = {0, 10, 20, 60};
static const uint16_t HAPTIC_VALUES[] = {0, 1, 2};
static const uint16_t LED_VALUES[] = {0, 1};
static const uint16_t BATT_VALUES[] = {0, 1, 2};

#define VALUES(a) (a), (sizeof(a) / sizeof((a)[0]))

static const setting_def_t SETTINGS[] = {
    {"brightness", STORAGE_SETTING_BRIGHTNESS, VALUES(BRIGHTNESS_VALUES)},
    {"dim_s", STORAGE_SETTING_DIM_S, VALUES(DIM_S_VALUES)},
    {"deep_min", STORAGE_SETTING_DEEP_MIN, VALUES(DEEP_MIN_VALUES)},
    {"haptic", STORAGE_SETTING_HAPTIC, VALUES(HAPTIC_VALUES)},
    {"led", STORAGE_SETTING_LED, VALUES(LED_VALUES)},
    {"batt", STORAGE_SETTING_BATT, VALUES(BATT_VALUES)},
};

#define SETTING_COUNT (sizeof(SETTINGS) / sizeof(SETTINGS[0]))

static console_rc_t send(cJSON *root)
{
    return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
}

static bool parse_long(const char *s, long min, long max, long *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    const long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < min || v > max) {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_flag(const char *s, bool *out)
{
    long v;
    if (!parse_long(s, 0, 1, &v)) {
        return false;
    }
    *out = v != 0;
    return true;
}

static console_rc_t post(const app_event_t *ev)
{
    return app_post(ev) ? CONSOLE_OK : console_fail(CONSOLE_ERR_BUSY, "app queue full");
}

static console_rc_t cmd_btn(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    btn_id_t id;
    if (argc != 3 || !btn_id_parse(argv[1], &id)) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "btn <approve|reject|action> <press|long>");
    }
    btn_kind_t kind;
    if (strcmp(argv[2], "press") == 0) {
        kind = BTN_CLICK;
    } else if (strcmp(argv[2], "long") == 0) {
        kind = BTN_LONG;
    } else {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "btn <approve|reject|action> <press|long>");
    }
    const app_event_t ev = {.id = APP_EV_BTN_INJECT, .t_ms = app_now_ms(), .arg.u8 = {(uint8_t)id, (uint8_t)kind}};
    return post(&ev);
}

static console_rc_t cmd_wake(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const app_event_t ev = {.id = APP_EV_USER_ACTIVITY, .t_ms = app_now_ms()};
    return post(&ev);
}

static console_rc_t fb_list(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "patterns");
    for (int i = 0; i < FB_SEQ_COUNT; i++) {
        const fb_seq_pattern_t *p = fb_seq_pattern((fb_seq_id_t)i);
        uint32_t total = 0;
        for (int j = 0; j < p->count; j++) {
            total += p->steps[j].ms;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", p->name);
        cJSON_AddNumberToObject(item, "priority", p->priority);
        cJSON_AddBoolToObject(item, "loop", p->loop);
        cJSON_AddNumberToObject(item, "steps", p->count);
        cJSON_AddNumberToObject(item, "ms", total);
        cJSON_AddItemToArray(list, item);
    }
    const fb_seq_id_t current = feedback_current();
    const fb_seq_pattern_t *p = fb_seq_pattern(current);
    if (p != NULL) {
        cJSON_AddStringToObject(root, "playing", p->name);
    } else {
        cJSON_AddNullToObject(root, "playing");
    }
    return send(root);
}

static console_rc_t cmd_fb(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    static const char USAGE[] = "fb list | fb play <pattern> [ms] | fb stop";
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        return fb_list();
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        feedback_stop();
        return CONSOLE_OK;
    }
    if ((argc == 3 || argc == 4) && strcmp(argv[1], "play") == 0) {
        fb_seq_id_t id;
        if (!fb_seq_find(argv[2], &id)) {
            return console_fail(CONSOLE_ERR_BAD_ARGS, "unknown pattern %s", argv[2]);
        }
        long ms = 0;
        if (argc == 4 && !parse_long(argv[3], 1, FB_DURATION_MAX, &ms)) {
            return console_fail(CONSOLE_ERR_BAD_ARGS, "duration 1..%d ms", FB_DURATION_MAX);
        }
        if (!feedback_play_for(id, (uint32_t)ms)) {
            return console_fail(CONSOLE_ERR_BUSY, "a higher-priority pattern is playing");
        }
        return CONSOLE_OK;
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "%s", USAGE);
}

static console_rc_t batt_show(void)
{
    power_battery_t b;
    power_get_battery(&b);
    if (!b.valid) {
        return console_fail(CONSOLE_ERR_INVALID_STATE, "no reading yet");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "raw", b.raw);
    cJSON_AddNumberToObject(root, "adcMv", b.adc_mv);
    cJSON_AddNumberToObject(root, "sampleMv", b.sample_mv);
    cJSON_AddNumberToObject(root, "mv", b.mv);
    cJSON_AddNumberToObject(root, "pct", b.pct);
    cJSON_AddStringToObject(root, "state", battery_state_name(b.state));
    cJSON_AddBoolToObject(root, "chrg", b.chrg);
    cJSON_AddBoolToObject(root, "stdby", b.stdby);
    cJSON_AddNumberToObject(root, "chrgEdges10s", b.chrg_edges_10s);
    cJSON_AddNumberToObject(root, "chrgEdgeRateHz", b.chrg_edge_rate_hz);
    cJSON_AddBoolToObject(root, "extPower", b.ext_power);
    cJSON_AddBoolToObject(root, "low", b.low);
    cJSON_AddBoolToObject(root, "critical", b.critical);
    cJSON_AddBoolToObject(root, "injected", b.injected);
    cJSON_AddStringToObject(root, "presence", battery_presence_name(b.presence));
    return send(root);
}

static console_rc_t write_setting(storage_setting_t id, uint16_t value)
{
    const esp_err_t err = storage_set_setting(id, value);
    if (err != ESP_OK) {
        return console_fail(CONSOLE_ERR_FAILED, "storage: %s", esp_err_to_name(err));
    }
    const app_event_t ev = {.id = APP_EV_SETTING_CHANGED, .arg.u32 = (uint32_t)id};
    if (!app_post(&ev)) {
        return console_fail(CONSOLE_ERR_BUSY, "saved, not applied: app queue full");
    }
    return CONSOLE_OK;
}

static console_rc_t cmd_batt(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    static const char USAGE[] = "batt | batt inject <mv> <chrg 0|1> <stdby 0|1> | batt inject off | "
                                "batt present auto|yes|no";
    if (argc == 1) {
        return batt_show();
    }
    if (argc == 3 && strcmp(argv[1], "inject") == 0 && strcmp(argv[2], "off") == 0) {
        power_inject_off();
        return CONSOLE_OK;
    }
    if (argc == 5 && strcmp(argv[1], "inject") == 0) {
        long mv;
        bool chrg;
        bool stdby;
        if (!parse_long(argv[2], 0, INJECT_MV_MAX, &mv) || !parse_flag(argv[3], &chrg) ||
            !parse_flag(argv[4], &stdby)) {
            return console_fail(CONSOLE_ERR_BAD_ARGS, "%s", USAGE);
        }
        power_inject((int32_t)mv, chrg, stdby);
        return CONSOLE_OK;
    }
    if (argc == 3 && strcmp(argv[1], "present") == 0) {
        for (int p = BATTERY_PRESENCE_AUTO; p <= BATTERY_PRESENCE_NO; p++) {
            if (strcmp(argv[2], battery_presence_name((battery_presence_t)p)) == 0) {
                return write_setting(STORAGE_SETTING_BATT, (uint16_t)p);
            }
        }
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "%s", USAGE);
}

static console_rc_t set_list(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "settings");
    for (size_t i = 0; i < SETTING_COUNT; i++) {
        const setting_def_t *def = &SETTINGS[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "key", def->key);
        cJSON_AddNumberToObject(item, "value", storage_get_setting(def->id));
        cJSON *allowed = cJSON_AddArrayToObject(item, "allowed");
        for (size_t v = 0; v < def->count; v++) {
            cJSON_AddItemToArray(allowed, cJSON_CreateNumber(def->values[v]));
        }
        cJSON_AddItemToArray(list, item);
    }
    return send(root);
}

static console_rc_t cmd_set(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc == 1) {
        return set_list();
    }
    if (argc != 3) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "set [<key> <value>]");
    }
    for (size_t i = 0; i < SETTING_COUNT; i++) {
        const setting_def_t *def = &SETTINGS[i];
        if (strcmp(argv[1], def->key) != 0) {
            continue;
        }
        long value;
        if (parse_long(argv[2], 0, UINT16_MAX, &value)) {
            for (size_t v = 0; v < def->count; v++) {
                if (def->values[v] == value) {
                    return write_setting(def->id, (uint16_t)value);
                }
            }
        }
        return console_fail(CONSOLE_ERR_BAD_ARGS, "value not allowed for %s (see set)", def->key);
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "unknown key %s", argv[1]);
}

static const console_cmd_t CMDS[] = {
    {"btn", "btn <approve|reject|action> <press|long>: inject a click or long press (wake rule applies)", cmd_btn},
    {"wake", "user activity without a button", cmd_wake},
    {"fb", "fb list | fb play <pattern> [ms] | fb stop: feedback patterns, steps reported as EVT fb", cmd_fb},
    {"batt", "batt | batt inject <mv> <chrg 0|1> <stdby 0|1> | batt inject off | batt present auto|yes|no "
             "(chrg/stdby 1 = pin asserted)", cmd_batt},
    {"set", "set [<key> <value>]: list or change a setting (persisted, applied at once)", cmd_set},
};

esp_err_t app_cmds_io_register(void)
{
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        ESP_RETURN_ON_ERROR(console_register(&CMDS[i]), TAG, "%s", CMDS[i].name);
    }
    return ESP_OK;
}
