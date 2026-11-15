#include "app_priv.h"

#include "app.h"
#include "battery.h"
#include "btn_fsm.h"
#include "console.h"
#include "fb_seq.h"
#include "feedback.h"
#include "pet_pose.h"
#include "power.h"
#include "proto.h"
#include "settings_menu.h"
#include "storage.h"
#include "ui.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_cmds_io";

#define INJECT_MV_MAX     5000
/* A `long` press outlasts both hold thresholds, so it is the hold or the commit, whichever the mode allows. */
#define BTN_LONG_MS       (BTN_FSM_COMMIT_HOLD_MS + 100)
#define BTN_HOLD_MAX_MS   10000
#define FB_DURATION_MAX   600000

/* Settings that are also on the Settings screen share its value tables (settings_menu.c). */
typedef struct {
    const char *key;
    storage_setting_t id;
    /** SETTINGS_ROW_COUNT for a setting the screen does not show. */
    settings_row_t row;
} setting_def_t;

static const uint16_t BATT_VALUES[] = {BATTERY_PRESENCE_AUTO, BATTERY_PRESENCE_YES, BATTERY_PRESENCE_NO};

static const setting_def_t SETTINGS[] = {
    {"brightness", STORAGE_SETTING_BRIGHTNESS, SETTINGS_ROW_BRIGHTNESS},
    {"dim_s", STORAGE_SETTING_DIM_S, SETTINGS_ROW_SCREEN_TIMEOUT},
    {"deep_min", STORAGE_SETTING_DEEP_MIN, SETTINGS_ROW_DEEP_SLEEP},
    {"haptic", STORAGE_SETTING_HAPTIC, SETTINGS_ROW_HAPTICS},
    {"led", STORAGE_SETTING_LED, SETTINGS_ROW_LED},
    {"batt", STORAGE_SETTING_BATT, SETTINGS_ROW_COUNT},
};

static const uint16_t *setting_values(const setting_def_t *def, size_t *count)
{
    if (def->row < SETTINGS_ROW_COUNT) {
        return settings_menu_choices(def->row, count);
    }
    *count = sizeof(BATT_VALUES) / sizeof(BATT_VALUES[0]);
    return BATT_VALUES;
}

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

static bool post_raw(btn_id_t id, btn_raw_t raw)
{
    const app_event_t ev = {.id = APP_EV_BTN_RAW, .t_ms = app_now_ms(), .arg.u8 = {(uint8_t)id, (uint8_t)raw}};
    return app_post(&ev);
}

/* Down, the hold in real time (the app's button tick runs meanwhile), up; the up is always posted. */
static console_rc_t cmd_btn(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    static const char *const USAGE = "btn <approve|reject|action> <press|long|hold MS>";
    btn_id_t id;
    if ((argc != 3 && argc != 4) || !btn_id_parse(argv[1], &id)) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, USAGE);
    }
    long ms = -1;
    if (argc == 3 && strcmp(argv[2], "press") == 0) {
        ms = 0;
    } else if (argc == 3 && strcmp(argv[2], "long") == 0) {
        ms = BTN_LONG_MS;
    } else if (argc == 4 && strcmp(argv[2], "hold") == 0 && !parse_long(argv[3], 0, BTN_HOLD_MAX_MS, &ms)) {
        ms = -1;
    }
    if (ms < 0) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, USAGE);
    }
    if (!post_raw(id, BTN_RAW_DOWN)) {
        return console_fail(CONSOLE_ERR_BUSY, "app queue full");
    }
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    while (!post_raw(id, BTN_RAW_UP)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return CONSOLE_OK;
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
    if (!app_setting_changed(id)) {
        return console_fail(CONSOLE_ERR_BUSY, "saved, not applied: app task busy");
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
        size_t count;
        const uint16_t *values = setting_values(def, &count);
        for (size_t v = 0; v < count; v++) {
            cJSON_AddItemToArray(allowed, cJSON_CreateNumber(values[v]));
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
        size_t count;
        const uint16_t *values = setting_values(def, &count);
        if (parse_long(argv[2], 0, UINT16_MAX, &value)) {
            for (size_t v = 0; v < count; v++) {
                if (values[v] == value) {
                    return write_setting(def->id, (uint16_t)value);
                }
            }
        }
        return console_fail(CONSOLE_ERR_BAD_ARGS, "value not allowed for %s (see set)", def->key);
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "unknown key %s", argv[1]);
}

#define PET_REQUEST_TIMEOUT_MS  3000

static console_rc_t pet_mood(const char *name)
{
    for (int m = 0; m < MOOD_COUNT; m++) {
        if (strcmp(name, proto_mood_name((proto_mood_t)m)) != 0) {
            continue;
        }
        char frame[64];
        snprintf(frame, sizeof(frame), "{\"type\":\"mood\",\"mood\":\"%s\"}", name);
        bool ok;
        char message[APP_REQUEST_MESSAGE_SIZE];
        if (app_request(APP_REQUEST_MSG, frame, NULL, PET_REQUEST_TIMEOUT_MS, &ok, message) != APP_REQUEST_DONE) {
            return console_fail(CONSOLE_ERR_BUSY, "no answer from the app task");
        }
        return ok ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "%s", message);
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "unknown mood %s", name);
}

static void add_pose(cJSON *obj, const pet_pose_t *p)
{
    char color[8];
    snprintf(color, sizeof(color), "#%06lx", (unsigned long)(p->body_color & 0xFFFFFF));
    cJSON_AddNumberToObject(obj, "eyeOpen", p->eye_open);
    cJSON_AddNumberToObject(obj, "eyeSize", p->eye_size);
    cJSON_AddNumberToObject(obj, "pupilSize", p->pupil_size);
    cJSON_AddNumberToObject(obj, "pupilDx", p->pupil_dx);
    cJSON_AddNumberToObject(obj, "pupilDy", p->pupil_dy);
    cJSON_AddNumberToObject(obj, "browAngle", p->brow_angle);
    cJSON_AddNumberToObject(obj, "mouthCurve", p->mouth_curve);
    cJSON_AddNumberToObject(obj, "mouthOpen", p->mouth_open);
    cJSON_AddNumberToObject(obj, "bobAmpPx", p->bob_amp_px);
    cJSON_AddNumberToObject(obj, "bobPeriodMs", p->bob_period_ms);
    cJSON_AddNumberToObject(obj, "blinkMinMs", p->blink_min_ms);
    cJSON_AddNumberToObject(obj, "blinkMaxMs", p->blink_max_ms);
    cJSON_AddNumberToObject(obj, "jitterPx", p->jitter_px);
    cJSON_AddStringToObject(obj, "bodyColor", color);
    cJSON_AddStringToObject(obj, "accessory", pet_accessory_name((pet_accessory_t)p->accessory));
    static const char *const GLANCE[] = {[PET_GLANCE_OFF] = "off", [PET_GLANCE_LOOK] = "look",
                                         [PET_GLANCE_SCAN] = "scan"};
    cJSON_AddStringToObject(obj, "glance", p->glance <= PET_GLANCE_SCAN ? GLANCE[p->glance] : "off");
}

static console_rc_t pet_pose(void)
{
    ui_pet_state_t st;
    ui_pet_get_state(&st);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mood", proto_mood_name(st.mood));
    cJSON_AddNumberToObject(root, "tweenPermille", st.tween_permille);
    cJSON_AddBoolToObject(root, "frozen", st.frozen);
    cJSON_AddBoolToObject(root, "animating", st.animating);
    cJSON_AddBoolToObject(root, "blinking", st.blinking);
    cJSON_AddStringToObject(root, "reaction", ui_pet_reaction_name(st.reaction));
    cJSON *drawn = cJSON_AddObjectToObject(root, "drawn");
    cJSON_AddNumberToObject(drawn, "eyeOpen", st.eye_open);
    cJSON_AddNumberToObject(drawn, "pupilDx", st.pupil_dx);
    cJSON_AddNumberToObject(drawn, "pupilDy", st.pupil_dy);
    cJSON_AddNumberToObject(drawn, "bobPx", st.bob_px);
    cJSON_AddNumberToObject(drawn, "jitterPx", st.jitter_px);
    add_pose(cJSON_AddObjectToObject(root, "pose"), &st.pose);
    return send(root);
}

static console_rc_t cmd_pet(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    static const char USAGE[] = "pet mood <mood> | pet pose | pet freeze on|off";
    if (argc == 3 && strcmp(argv[1], "mood") == 0) {
        return pet_mood(argv[2]);
    }
    if (argc == 2 && strcmp(argv[1], "pose") == 0) {
        return pet_pose();
    }
    if (argc == 3 && strcmp(argv[1], "freeze") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        ui_pet_freeze(strcmp(argv[2], "on") == 0);
        return CONSOLE_OK;
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "%s", USAGE);
}

static console_rc_t cmd_factory_reset(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc != 2 || strcmp(argv[1], "--yes-erase-wifi-and-pairing") != 0) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "factory-reset --yes-erase-wifi-and-pairing");
    }
    bool ok;
    char message[APP_REQUEST_MESSAGE_SIZE];
    if (app_request(APP_REQUEST_FACTORY_RESET, NULL, NULL, PET_REQUEST_TIMEOUT_MS, &ok, message) != APP_REQUEST_DONE) {
        return console_fail(CONSOLE_ERR_BUSY, "no answer from the app task");
    }
    return CONSOLE_OK;
}

static const console_cmd_t CMDS[] = {
    {"btn", "btn <approve|reject|action> <press|long|hold MS>: press a button (long: 1.3 s, past both hold thresholds)", cmd_btn},
    {"wake", "user activity without a button", cmd_wake},
    {"fb", "fb list | fb play <pattern> [ms] | fb stop: feedback patterns, steps reported as EVT fb", cmd_fb},
    {"batt", "batt | batt inject <mv> <chrg 0|1> <stdby 0|1> | batt inject off | batt present auto|yes|no "
             "(chrg/stdby 1 = pin asserted)", cmd_batt},
    {"set", "set [<key> <value>]: list or change a setting (persisted, applied at once)", cmd_set},
    {"pet", "pet mood <mood> | pet pose | pet freeze on|off: drive and inspect the face", cmd_pet},
    {"factory-reset", "factory-reset --yes-erase-wifi-and-pairing: erase WiFi, pairing and settings, then restart",
     cmd_factory_reset},
};

esp_err_t app_cmds_io_register(void)
{
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        ESP_RETURN_ON_ERROR(console_register(&CMDS[i]), TAG, "%s", CMDS[i].name);
    }
    return ESP_OK;
}
