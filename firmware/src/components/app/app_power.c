#include "app_priv.h"

#include "app.h"
#include "battery.h"
#include "console.h"
#include "display.h"
#include "feedback.h"
#include "power.h"
#include "power_fsm.h"
#include "storage.h"
#include "ui.h"
#include "wifi.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "pwr";

#define OFF_SAMPLE_PERIOD_MS  10000
/* Architecture 4.8: the WebSocket stop is a net-worker job that may be stuck in a connect. */
#define WS_STOP_WAIT_MS       3000
#define WAKE_CHECK_STEP_MS    100
/* Lets the console write the last lines before the USB port goes away. */
#define CONSOLE_DRAIN_MS      100
#define REQUEST_TIMEOUT_MS    1000
#define WAKE_S_MAX            86400

#if CONFIG_DEVOTCHI_PWR_TEST_SECONDS
#define DEEP_UNIT_MS 1000u
#else
#define DEEP_UNIT_MS 60000u
#endif

/* App task only. */
static power_fsm_t s_fsm;
static power_fsm_output_t s_out;
static bool s_applied;
static bool s_dry_run_reported;
static bool s_sleeping;
static bool s_wifi_ps_set;
/* A console `power sleep deep` waiting for the end of its request; its timer wake in seconds. */
static bool s_forced_sleep;
static uint32_t s_forced_wake_s;
static uint16_t s_dim_s;
static uint16_t s_deep_min;
static uint8_t s_brightness;

/* Written by the app task, read by the console under s_view_mux. */
static portMUX_TYPE s_view_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_dry_run;
static bool s_dry_run_set;
static bool s_usb_seen_in_grace;
static power_fsm_input_t s_view_in;
static power_fsm_output_t s_view_out;
static bool s_view_forced;

static bool dry_run(void)
{
    taskENTER_CRITICAL(&s_view_mux);
    const bool on = s_dry_run_set ? s_dry_run : s_usb_seen_in_grace;
    taskEXIT_CRITICAL(&s_view_mux);
    return on;
}

static void load_settings(void)
{
    s_dim_s = storage_get_setting(STORAGE_SETTING_DIM_S);
    s_deep_min = storage_get_setting(STORAGE_SETTING_DEEP_MIN);
    s_brightness = (uint8_t)storage_get_setting(STORAGE_SETTING_BRIGHTNESS);
}

static void build_input(uint32_t now, power_fsm_input_t *in)
{
    app_activity_t act;
    app_get_activity(&act);
    power_battery_t b;
    power_get_battery(&b);
    const bool usb_host = power_usb_host();
    if (usb_host && now < (uint32_t)CONFIG_DEVOTCHI_PWR_BOOT_GRACE_S * 1000u) {
        taskENTER_CRITICAL(&s_view_mux);
        s_usb_seen_in_grace = true;
        taskEXIT_CRITICAL(&s_view_mux);
    }
    *in = (power_fsm_input_t){
        .now_ms = now,
        .boot_ms = 0,
        .last_user_ms = act.last_user_ms,
        .last_agent_ms = act.last_agent_ms,
        .pending_count = app_flow_pending_count(),
        .card_visible = app_flow_card_visible(),
        .ext_power = b.ext_power,
        .usb_host = usb_host,
        .provisioning = act.state == APP_STATE_PROVISIONING,
        .batt_present = b.valid && b.state != BATTERY_STATE_ABSENT,
        .batt_low = b.low,
        .batt_critical = b.critical,
        .screen_timeout_s = s_dim_s,
        .deep_timeout_ms = (uint32_t)s_deep_min * DEEP_UNIT_MS,
        .usb_deep_timeout_ms = (uint32_t)CONFIG_DEVOTCHI_PWR_USB_DEEP_MIN * 60000u,
        .boot_grace_s = CONFIG_DEVOTCHI_PWR_BOOT_GRACE_S,
#if CONFIG_DEVOTCHI_PWR_LIGHT_SLEEP
        .light_sleep_enabled = true,
#endif
        .dry_run = dry_run(),
        .brightness_pct = s_brightness,
        .dim_pct = CONFIG_DEVOTCHI_DIM_BRIGHTNESS_PCT,
    };
}

static void publish(const power_fsm_input_t *in, const power_fsm_output_t *out)
{
    taskENTER_CRITICAL(&s_view_mux);
    s_view_in = *in;
    s_view_out = *out;
    s_view_forced = s_fsm.forced;
    taskEXIT_CRITICAL(&s_view_mux);
}

static void deep_sleep_evt(const char *reason, bool dry, uint32_t timer_s)
{
    char evt[128];
    snprintf(evt, sizeof(evt), "{\"evt\":\"power\",\"action\":\"deep_sleep\",\"reason\":\"%s\",\"dryRun\":%s,\"timerS\":%lu}",
             reason, dry ? "true" : "false", (unsigned long)timer_s);
    console_evt(evt);
}

static void deep_sleep(uint32_t now, const char *reason, bool battery, uint32_t timer_s) __attribute__((noreturn));

static void deep_sleep(uint32_t now, const char *reason, bool battery, uint32_t timer_s)
{
    s_sleeping = true;
    ESP_LOGI(TAG, "entering deep sleep (%s)", reason);
    deep_sleep_evt(reason, false, timer_s);
    if (battery) {
        app_telemetry_send_final(now);
    }
    power_set_slept_for_battery(battery);
    app_link_suspend(now);
    if (!app_wait_link_idle(WS_STOP_WAIT_MS)) {
        ESP_LOGW(TAG, "websocket stop did not finish in %d ms", WS_STOP_WAIT_MS);
    }
    wifi_stop();
    feedback_stop();
    ui_pet_set_running(false);
    ui_set_running(false);
    display_sleep(true);
    vTaskDelay(pdMS_TO_TICKS(CONSOLE_DRAIN_MS));
    power_deep_sleep(timer_s);
}

static void stage_evt(power_stage_t from, const power_fsm_output_t *out)
{
    char evt[128];
    snprintf(evt, sizeof(evt), "{\"evt\":\"power\",\"stage\":\"%s\",\"from\":\"%s\",\"idleS\":%lu}",
             power_stage_name(out->stage), power_stage_name(from), (unsigned long)(out->idle_ms / 1000));
    console_evt(evt);
}

static void apply(uint32_t now, const power_fsm_output_t *out)
{
    const bool first = !s_applied;
    const power_fsm_output_t prev = s_out;
    s_out = *out;
    s_applied = true;

    /* Locks are taken before the screen work and released after it, so redraws always run at full speed. */
    if (out->hold_cpu_lock) {
        power_pm_set(out->hold_cpu_lock, out->allow_light_sleep);
    }
    if (first || out->stage != prev.stage) {
        const bool was_off = !first && prev.stage == POWER_STAGE_OFF;
        if (out->stage == POWER_STAGE_OFF) {
            ui_pet_set_running(false);
            ui_set_running(false);
            display_sleep(true);
            power_set_sample_period_ms(OFF_SAMPLE_PERIOD_MS);
        } else {
            display_set_brightness(out->backlight_pct);
            if (was_off) {
                power_set_sample_period_ms(POWER_DEFAULT_PERIOD_MS);
                /* Rendering paused while the panel slept; bring the content up to date before it is shown. */
                app_link_render(now);
                display_sleep(false);
                ui_set_running(true);
            }
            ui_pet_set_running(out->stage == POWER_STAGE_ACTIVE);
        }
        if (!first) {
            stage_evt(prev.stage, out);
        }
    } else if (out->stage != POWER_STAGE_OFF && out->backlight_pct != prev.backlight_pct) {
        display_set_brightness(out->backlight_pct);
    }
    /* Retried until the driver has started (the first wifi_connect). */
    if (!s_wifi_ps_set || out->wifi_ps != prev.wifi_ps) {
        s_wifi_ps_set = wifi_set_power_save(out->wifi_ps == POWER_WIFI_PS_MAX) == ESP_OK;
    }
    if (!out->hold_cpu_lock) {
        power_pm_set(out->hold_cpu_lock, out->allow_light_sleep);
    }
    if (first || out->show_lowbatt != prev.show_lowbatt) {
        app_link_render(now);
    }
    if (out->low_toast) {
        app_toast_kind(UI_TOAST_BATTERY, "Battery low");
    }
}

void app_power_update(uint32_t now)
{
    if (s_sleeping) {
        return;
    }
    if (s_forced_sleep) {
        deep_sleep(now, "forced", false, s_forced_wake_s);
    }
    power_fsm_input_t in;
    build_input(now, &in);
    power_fsm_output_t out;
    power_fsm_update(&s_fsm, &in, &out);
    publish(&in, &out);
    apply(now, &out);

    if (!out.enter_deep_sleep) {
        s_dry_run_reported = false;
        return;
    }
    const char *reason = power_deep_reason_name(out.deep_reason);
    if (in.dry_run) {
        if (!s_dry_run_reported) {
            s_dry_run_reported = true;
            ESP_LOGI(TAG, "dry-run: would enter deep sleep (%s)", reason);
            deep_sleep_evt(reason, true, 0);
        }
        return;
    }
    deep_sleep(now, reason, out.deep_reason == POWER_DEEP_BATTERY, 0);
}

power_stage_t app_power_stage(void)
{
    return s_applied ? s_out.stage : POWER_STAGE_ACTIVE;
}

bool app_power_screen_off(void)
{
    return s_applied && s_out.stage == POWER_STAGE_OFF;
}

bool app_power_lowbatt(void)
{
    return s_applied && s_out.show_lowbatt;
}

void app_power_on_setting(storage_setting_t id)
{
    if (id == STORAGE_SETTING_DIM_S || id == STORAGE_SETTING_DEEP_MIN || id == STORAGE_SETTING_BRIGHTNESS) {
        load_settings();
    }
}

/* ---- battery-wake decision (architecture 4.8, low battery) ---- */

typedef struct {
    int32_t mv;
    bool ext_power;
    bool batt_present;
    bool usb_host;
} wake_sample_t;

static void wake_input(const wake_sample_t *s, uint32_t now, bool dry, power_fsm_input_t *in)
{
    build_input(now, in);
    in->ext_power = s->ext_power;
    in->usb_host = s->usb_host;
    in->batt_present = s->batt_present;
    in->batt_low = s->mv < CONFIG_DEVOTCHI_BATT_LOW_MV;
    in->batt_critical = s->mv < CONFIG_DEVOTCHI_BATT_CRIT_MV;
    in->wake_reason_battery = true;
    in->dry_run = dry;
    in->pending_count = 0;
    in->card_visible = false;
}

/* A burst of readings: the filter has no history yet at this point of the boot. */
static bool read_burst_mv(int32_t *mv)
{
    int32_t samples[BATTERY_MEDIAN_N];
    for (int i = 0; i < BATTERY_MEDIAN_N; i++) {
        if (power_read_battery_mv_now(&samples[i]) != ESP_OK) {
            return false;
        }
    }
    *mv = battery_median5(samples);
    return true;
}

static bool presence_allows_battery(void)
{
    return storage_get_setting(STORAGE_SETTING_BATT) != BATTERY_PRESENCE_NO;
}

static void live_charger(wake_sample_t *s)
{
    bool chrg;
    bool stdby;
    power_read_charger_now(&chrg, &stdby);
    s->ext_power = chrg || stdby;
    s->usb_host = power_usb_host();
}

void app_power_boot_check(void)
{
    if (!power_slept_for_battery()) {
        return;
    }
    wake_sample_t s = {.batt_present = presence_allows_battery()};
    if (!read_burst_mv(&s.mv)) {
        ESP_LOGW(TAG, "battery wake: no ADC reading, booting normally");
        power_set_slept_for_battery(false);
        return;
    }
    ESP_LOGI(TAG, "battery wake: %ld mV", (long)s.mv);
    power_fsm_t fsm;
    power_fsm_init(&fsm);
    bool shown = false;
    for (;;) {
        const uint32_t now = app_now_ms();
        live_charger(&s);
        power_fsm_input_t in;
        /* A real decision: a USB host always cancels it, whatever the dry-run setting. */
        wake_input(&s, now, false, &in);
        power_fsm_output_t out;
        power_fsm_update(&fsm, &in, &out);
        if (out.enter_deep_sleep) {
            if (dry_run()) {
                deep_sleep_evt("battery", true, 0);
                break;
            }
            deep_sleep(now, "battery", true, 0);
        }
        if (!out.wake_check_pending) {
            break;
        }
        if (out.show_lowbatt && !shown) {
            shown = true;
            app_show_screen(UI_SCREEN_LOWBATT);
        }
        vTaskDelay(pdMS_TO_TICKS(WAKE_CHECK_STEP_MS));
    }
    ESP_LOGI(TAG, "battery wake: booting normally (%ld mV, ext %d, usb %d)", (long)s.mv, s.ext_power, s.usb_host);
    power_set_slept_for_battery(false);
}

/* ---- console requests (app task) ---- */

void app_power_on_request(app_request_t *req)
{
    if (s_sleeping) {
        app_request_complete(false, "going to sleep");
        return;
    }
    if (strcmp(req->text, "stage") == 0) {
        power_stage_t stage;
        if (!power_stage_parse(req->arg, &stage)) {
            app_request_complete(false, "expected active, dim or off");
            return;
        }
        app_activity_t act;
        app_get_activity(&act);
        power_fsm_force_stage(&s_fsm, stage, act.last_user_ms);
        app_request_complete(true, "");
        return;
    }
    if (strcmp(req->text, "dry-run") == 0) {
        taskENTER_CRITICAL(&s_view_mux);
        s_dry_run_set = true;
        s_dry_run = strcmp(req->arg, "on") == 0;
        taskEXIT_CRITICAL(&s_view_mux);
        s_dry_run_reported = false;
        app_request_complete(true, "");
        return;
    }
    if (strcmp(req->text, "sleep") == 0) {
        /* Started by app_power_update after this event, outside the link's request handling. */
        s_forced_sleep = true;
        s_forced_wake_s = (uint32_t)strtoul(req->arg, NULL, 10);
        app_request_complete(true, "sleeping");
        return;
    }
    app_request_complete(false, "unknown power request");
}

/* ---- console command (console task) ---- */

static console_rc_t request(const char *op, const char *arg)
{
    bool ok;
    char message[APP_REQUEST_MESSAGE_SIZE];
    switch (app_request(APP_REQUEST_POWER, op, arg, REQUEST_TIMEOUT_MS, &ok, message)) {
    case APP_REQUEST_DONE:
        return ok ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "%s", message);
    case APP_REQUEST_BUSY:
        return console_fail(CONSOLE_ERR_BUSY, "app task busy with an earlier request");
    case APP_REQUEST_TIMEOUT:
        break;
    }
    return console_fail(CONSOLE_ERR_BUSY, "no answer from the app task");
}

static void add_guards(cJSON *root, uint32_t blockers)
{
    cJSON *guards = cJSON_AddArrayToObject(root, "guards");
    for (int i = 0; i < POWER_BLOCK_COUNT; i++) {
        if (blockers & (1u << i)) {
            cJSON_AddItemToArray(guards, cJSON_CreateString(power_block_name(1u << i)));
        }
    }
}

static console_rc_t show(void)
{
    taskENTER_CRITICAL(&s_view_mux);
    const power_fsm_input_t in = s_view_in;
    const power_fsm_output_t out = s_view_out;
    const bool forced = s_view_forced;
    taskEXIT_CRITICAL(&s_view_mux);
    power_pm_state_t pm;
    power_pm_get(&pm);
    power_wake_t wake;
    power_get_wake(&wake);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "stage", power_stage_name(out.stage));
    cJSON_AddBoolToObject(root, "forced", forced);
    cJSON_AddStringToObject(root, "source", in.ext_power ? "external" : "battery");
    cJSON_AddBoolToObject(root, "usbHost", power_usb_host());
    cJSON_AddNumberToObject(root, "idleS", out.idle_ms / 1000);
    cJSON_AddNumberToObject(root, "agentIdleS", out.agent_idle_ms / 1000);
    cJSON_AddNumberToObject(root, "pending", in.pending_count);
    cJSON_AddBoolToObject(root, "cardVisible", in.card_visible);
    cJSON *t = cJSON_AddObjectToObject(root, "timeouts");
    cJSON_AddNumberToObject(t, "dimS", out.dim_ms / 1000);
    cJSON_AddNumberToObject(t, "offS", out.off_ms / 1000);
    cJSON_AddNumberToObject(t, "deepS", out.deep_ms / 1000);
    cJSON_AddNumberToObject(t, "bootGraceS", in.boot_grace_s);
    add_guards(root, out.blockers);
    cJSON_AddBoolToObject(root, "deepSleepDue", out.enter_deep_sleep);
    cJSON_AddBoolToObject(root, "dryRun", dry_run());
    cJSON_AddBoolToObject(root, "lightSleepAllowed", out.allow_light_sleep);
    cJSON_AddBoolToObject(root, "lowbatt", out.show_lowbatt);
    cJSON_AddBoolToObject(root, "cpuLock", pm.cpu_lock);
    cJSON_AddBoolToObject(root, "noLightSleepLock", pm.no_light_sleep_lock);
    cJSON_AddNumberToObject(root, "cpuMhz", pm.cpu_mhz);
    cJSON_AddBoolToObject(root, "pmConfigured", pm.configured);
    cJSON_AddBoolToObject(root, "panelAsleep", display_is_asleep());
    cJSON_AddNumberToObject(root, "backlightPct", display_backlight_pct());
    cJSON_AddBoolToObject(root, "uiParked", !ui_is_running());
    cJSON_AddStringToObject(root, "wifiPs", out.wifi_ps == POWER_WIFI_PS_MAX ? "max" : "min");
    cJSON_AddBoolToObject(root, "deepSleepWake", wake.deep_sleep);
    cJSON_AddStringToObject(root, "wakeButton", power_wake_button_name(&wake));
    cJSON_AddBoolToObject(root, "wakeTimer", wake.timer);
    cJSON_AddBoolToObject(root, "sleptForBattery", power_slept_for_battery());
    return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
}

/* Replays the boot decision of a battery-protection wake without sleeping. */
static console_rc_t wake_test(void)
{
    if (!dry_run()) {
        return console_fail(CONSOLE_ERR_INVALID_STATE, "dry-run only (power dry-run on)");
    }
    power_battery_t b;
    power_get_battery(&b);
    wake_sample_t s = {.batt_present = presence_allows_battery()};
    if (b.injected) {
        s.mv = b.mv;
        s.ext_power = b.ext_power;
        s.usb_host = power_usb_host();
    } else {
        if (!read_burst_mv(&s.mv)) {
            return console_fail(CONSOLE_ERR_FAILED, "adc");
        }
        live_charger(&s);
    }
    power_fsm_t fsm;
    power_fsm_init(&fsm);
    power_fsm_output_t out = {0};
    bool resleep = false;
    uint32_t t = 0;
    for (; t <= POWER_FSM_WAKE_LOWBATT_MS; t += WAKE_CHECK_STEP_MS) {
        power_fsm_input_t in;
        wake_input(&s, t, true, &in);
        in.last_user_ms = 0;
        in.last_agent_ms = 0;
        power_fsm_update(&fsm, &in, &out);
        if (out.enter_deep_sleep || !out.wake_check_pending) {
            resleep = out.enter_deep_sleep && out.deep_reason == POWER_DEEP_BATTERY;
            break;
        }
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "decision", resleep ? "resleep" : "boot");
    cJSON_AddNumberToObject(root, "mv", s.mv);
    cJSON_AddBoolToObject(root, "injected", b.injected);
    cJSON_AddBoolToObject(root, "critical", s.mv < CONFIG_DEVOTCHI_BATT_CRIT_MV);
    cJSON_AddBoolToObject(root, "extPower", s.ext_power);
    cJSON_AddBoolToObject(root, "batteryPresent", s.batt_present);
    cJSON_AddBoolToObject(root, "usbHost", s.usb_host);
    cJSON_AddNumberToObject(root, "lowbattMs", resleep ? t : 0);
    return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
}

static console_rc_t cmd_power(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc == 1) {
        return show();
    }
    if (argc == 3 && strcmp(argv[1], "stage") == 0) {
        return request("stage", argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "dry-run") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        return request("dry-run", argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "wake-test") == 0 && strcmp(argv[2], "battery") == 0) {
        return wake_test();
    }
    if ((argc == 4 || argc == 5) && strcmp(argv[1], "sleep") == 0 && strcmp(argv[2], "deep") == 0) {
        char *end = NULL;
        const unsigned long wake_s = strtoul(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || wake_s > WAKE_S_MAX) {
            return console_fail(CONSOLE_ERR_BAD_ARGS, "wakeS: 0..%d (0 = buttons only)", WAKE_S_MAX);
        }
        if (argc != 5 || strcmp(argv[4], "--force") != 0) {
            return console_fail(CONSOLE_ERR_INVALID_STATE,
                                "refused: needs --force (sleeps even with a USB host; any button wakes)");
        }
        return request("sleep", argv[3]);
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "power | power stage active|dim|off | power dry-run on|off | "
                                              "power wake-test battery | power sleep deep <wakeS> --force");
}

static const console_cmd_t CMD = {
    "power",
    "power: stage, guards and locks | power stage active|dim|off (until the next activity) | power dry-run on|off | "
    "power wake-test battery | power sleep deep <wakeS> --force",
    cmd_power,
};

esp_err_t app_power_init(void)
{
    power_fsm_init(&s_fsm);
    load_settings();
    return console_register(&CMD);
}
