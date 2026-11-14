#include "app.h"

#include "app_priv.h"
#include "app_tasks.h"
#include "battery.h"
#include "buttons.h"
#include "console.h"
#include "display.h"
#include "fb_seq.h"
#include "feedback.h"
#include "power.h"
#include "storage.h"
#include "ui.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "app";

#define POST_TIMEOUT_MS      10
#define SNAPSHOT_TIMEOUT_MS  100
#define TICK_PERIOD_US       1000000
#define SETTING_TIMEOUT_MS   500

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_snapshot_lock;
static esp_timer_handle_t s_tick_timer;
static SemaphoreHandle_t s_setting_applied;

/* Written by the app task under s_snapshot_lock; other tasks read it through app_get_snapshot(). */
static app_snapshot_t s_snapshot;

/* App task only. */
static btn_fsm_t s_btn;
static bool s_fb_playing;
static uint32_t s_fb_start_ms;

const char *app_state_name(app_state_t state)
{
    switch (state) {
    case APP_STATE_BOOT:
        return "boot";
    case APP_STATE_PROVISIONING:
        return "provisioning";
    case APP_STATE_WIFI_CONNECTING:
        return "wifi_connecting";
    case APP_STATE_WIFI_FAILED:
        return "wifi_failed";
    case APP_STATE_PAIRING:
        return "pairing";
    case APP_STATE_SERVER_CONNECTING:
        return "server_connecting";
    case APP_STATE_ONLINE:
        return "online";
    case APP_STATE_OFFLINE:
        return "offline";
    }
    return "";
}

const char *app_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_SW:
        return "sw";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_USB:
        return "usb";
    default:
        return "other";
    }
}

void app_get_device_id(char id[APP_DEVICE_ID_SIZE])
{
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(id, APP_DEVICE_ID_SIZE, "dv-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

uint32_t app_uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

uint32_t app_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool app_post(const app_event_t *ev)
{
    if (xQueueSend(s_queue, ev, pdMS_TO_TICKS(POST_TIMEOUT_MS)) == pdTRUE) {
        return true;
    }
    if (xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(POST_TIMEOUT_MS)) == pdTRUE) {
        s_snapshot.events_dropped++;
        xSemaphoreGive(s_snapshot_lock);
    }
    return false;
}

bool app_post_from_isr(const app_event_t *ev)
{
    BaseType_t woken = pdFALSE;
    const bool ok = xQueueSendFromISR(s_queue, ev, &woken) == pdTRUE;
    portYIELD_FROM_ISR(woken);
    return ok;
}

void app_get_snapshot(app_snapshot_t *out)
{
    *out = (app_snapshot_t){0};
    if (xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(SNAPSHOT_TIMEOUT_MS)) == pdTRUE) {
        *out = s_snapshot;
        xSemaphoreGive(s_snapshot_lock);
    }
}

static void show_screen(ui_screen_id_t id)
{
    if (ui_show_screen(id) != ESP_OK) {
        ESP_LOGE(TAG, "screen %s unavailable", ui_screen_name(id));
        return;
    }
    char evt[64];
    snprintf(evt, sizeof(evt), "{\"evt\":\"screen\",\"id\":\"%s\"}", ui_screen_name(id));
    console_evt(evt);
}

static void fill_diag_screen(void)
{
    char line[STORAGE_URL_SIZE + 16];
    char id[APP_DEVICE_ID_SIZE];
    char url[STORAGE_URL_SIZE];
    storage_url_source_t source;

    app_get_device_id(id);
    snprintf(line, sizeof(line), "fw %s  reset %s", esp_app_get_description()->version, app_reset_reason());
    ui_diag_set_line(0, line);
    snprintf(line, sizeof(line), "id %s", id);
    ui_diag_set_line(1, line);
    snprintf(line, sizeof(line), "nvs %s  wifi %s  paired %s", storage_is_degraded() ? "error" : "ok",
             storage_has_wifi() ? "yes" : "no", storage_is_paired() ? "yes" : "no");
    ui_diag_set_line(2, line);
    storage_get_server_url(url, &source);
    snprintf(line, sizeof(line), "%s (%s)", url, source == STORAGE_URL_SOURCE_NVS ? "nvs" : "default");
    ui_diag_set_line(3, line);
}

static bool snapshot_lock(void)
{
    return xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(SNAPSHOT_TIMEOUT_MS)) == pdTRUE;
}

static void user_activity(uint32_t now_ms)
{
    if (snapshot_lock()) {
        s_snapshot.last_user_ms = now_ms;
        xSemaphoreGive(s_snapshot_lock);
    }
}

static void on_button(const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (snapshot_lock()) {
        s_snapshot.has_last_btn = true;
        s_snapshot.last_btn = *out;
        s_snapshot.last_btn_ms = now_ms;
        xSemaphoreGive(s_snapshot_lock);
    }

    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"btn\",\"id\":\"%s\",\"kind\":\"%s\",\"swallowed\":%s}",
             btn_id_name(out->id), btn_kind_name(out->kind), out->swallowed ? "true" : "false");
    console_evt(evt);

    char line[48];
    snprintf(line, sizeof(line), "btn %s %s%s", btn_id_name(out->id), btn_kind_name(out->kind),
             out->swallowed ? " (swallowed)" : "");
    ui_diag_set_line(APP_DIAG_LINE_BTN, line);
}

static void handle_btn_raw(const app_event_t *ev)
{
    const btn_id_t id = (btn_id_t)ev->arg.u8[0];
    const btn_raw_t raw = (btn_raw_t)ev->arg.u8[1];
    if (raw == BTN_RAW_DOWN) {
        user_activity(ev->t_ms);
    }
    ESP_LOGD(TAG, "raw %s %d", btn_id_name(id), raw);
    btn_fsm_out_t out;
    if (btn_fsm_raw(&s_btn, id, raw, ev->t_ms, &out)) {
        on_button(&out, ev->t_ms);
    }
}

static void handle_btn_inject(const app_event_t *ev)
{
    const btn_id_t id = (btn_id_t)ev->arg.u8[0];
    if (id >= BTN_COUNT) {
        return;
    }
    user_activity(ev->t_ms);
    btn_fsm_out_t out;
    btn_fsm_inject(&s_btn, id, (btn_kind_t)ev->arg.u8[1], ev->t_ms, &out);
    on_button(&out, ev->t_ms);
}

static void handle_fb_step(const app_event_t *ev)
{
    const fb_seq_pattern_t *p = fb_seq_pattern((fb_seq_id_t)ev->arg.u8[0]);
    if (p == NULL) {
        return;
    }
    const uint8_t step = ev->arg.u8[1];
    /* A looping pattern passes step 0 again; only the first step after an end starts the clock. */
    if (!s_fb_playing) {
        s_fb_start_ms = ev->t_ms;
    }
    s_fb_playing = step != FEEDBACK_STEP_END;
    const uint32_t t = ev->t_ms - s_fb_start_ms;
    char evt[128];
    if (step == FEEDBACK_STEP_END) {
        snprintf(evt, sizeof(evt), "{\"evt\":\"fb\",\"pattern\":\"%s\",\"end\":true,\"t\":%lu}", p->name,
                 (unsigned long)t);
    } else if (step < p->count) {
        snprintf(evt, sizeof(evt),
                 "{\"evt\":\"fb\",\"pattern\":\"%s\",\"step\":%u,\"led\":%u,\"motor\":%u,\"ms\":%u,\"t\":%lu}",
                 p->name, step, ev->arg.u8[2], ev->arg.u8[3], p->steps[step].ms, (unsigned long)t);
    } else {
        return;
    }
    console_evt(evt);
}

static void handle_power(const app_event_t *ev)
{
    if (ev->arg.u32 != 0) {
        user_activity(ev->t_ms);
    }
    power_battery_t b;
    power_get_battery(&b);

    char evt[192];
    snprintf(evt, sizeof(evt),
             "{\"evt\":\"power\",\"battery\":{\"state\":\"%s\",\"pct\":%d,\"mv\":%ld,\"extPower\":%s,"
             "\"extPowerChanged\":%s,\"low\":%s,\"critical\":%s}}",
             battery_state_name(b.state), b.pct, (long)b.mv, b.ext_power ? "true" : "false",
             ev->arg.u32 != 0 ? "true" : "false", b.low ? "true" : "false", b.critical ? "true" : "false");
    console_evt(evt);

    char line[64];
    if (b.pct >= 0) {
        snprintf(line, sizeof(line), "batt %s %d%%  %ld mV%s%s%s", battery_state_name(b.state), b.pct, (long)b.mv,
                 b.ext_power ? "  ext" : "", b.low ? "  LOW" : "", b.critical ? "  CRITICAL" : "");
    } else {
        snprintf(line, sizeof(line), "batt %s  %ld mV%s", battery_state_name(b.state), (long)b.mv,
                 b.ext_power ? "  ext" : "");
    }
    ui_diag_set_line(APP_DIAG_LINE_BATT, line);
}

static void handle_setting(storage_setting_t id)
{
    switch (id) {
    case STORAGE_SETTING_BRIGHTNESS:
        display_set_brightness((uint8_t)storage_get_setting(STORAGE_SETTING_BRIGHTNESS));
        break;
    case STORAGE_SETTING_HAPTIC:
    case STORAGE_SETTING_LED:
        feedback_reload_settings();
        break;
    case STORAGE_SETTING_BATT:
        power_reload_settings();
        break;
    case STORAGE_SETTING_DIM_S:
    case STORAGE_SETTING_DEEP_MIN:
    case STORAGE_SETTING_COUNT:
        break;
    }
}

static void handle_event(const app_event_t *ev)
{
    switch (ev->id) {
    case APP_EV_TICK:
        if (ui_current_screen() == UI_SCREEN_SPLASH) {
            show_screen(UI_SCREEN_DIAG);
        }
        break;
    case APP_EV_BTN_RAW:
        handle_btn_raw(ev);
        break;
    case APP_EV_BTN_INJECT:
        handle_btn_inject(ev);
        break;
    case APP_EV_USER_ACTIVITY:
        user_activity(ev->t_ms);
        break;
    case APP_EV_FB_STEP:
        handle_fb_step(ev);
        break;
    case APP_EV_POWER:
        handle_power(ev);
        break;
    case APP_EV_SETTING_CHANGED:
        handle_setting((storage_setting_t)ev->arg.u32);
        xSemaphoreGive(s_setting_applied);
        break;
    }
}

static void app_task(void *arg)
{
    (void)arg;
    fill_diag_screen();
    ui_diag_set_line(APP_DIAG_LINE_BATT, "batt --");
    ui_diag_set_line(APP_DIAG_LINE_BTN, "btn --");
    for (;;) {
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) == pdTRUE) {
            handle_event(&ev);
        }
    }
}

bool app_setting_changed(storage_setting_t id)
{
    /* A wait that timed out leaves its signal behind; it must not answer this request. */
    xSemaphoreTake(s_setting_applied, 0);
    const app_event_t ev = {.id = APP_EV_SETTING_CHANGED, .arg.u32 = (uint32_t)id};
    return app_post(&ev) && xSemaphoreTake(s_setting_applied, pdMS_TO_TICKS(SETTING_TIMEOUT_MS)) == pdTRUE;
}

static void tick_cb(void *arg)
{
    (void)arg;
    const app_event_t ev = {.id = APP_EV_TICK};
    app_post(&ev);
}

static void on_button_raw(btn_id_t id, btn_raw_t raw, uint32_t now_ms)
{
    const app_event_t ev = {.id = APP_EV_BTN_RAW, .t_ms = now_ms, .arg.u8 = {(uint8_t)id, (uint8_t)raw}};
    app_post(&ev);
}

static void on_feedback_step(const feedback_step_t *step)
{
    const app_event_t ev = {
        .id = APP_EV_FB_STEP,
        .t_ms = step->now_ms,
        .arg.u8 = {(uint8_t)step->pattern, step->step, step->led_pct, step->motor_pct},
    };
    app_post(&ev);
}

static void on_power(bool ext_power_changed)
{
    const app_event_t ev = {.id = APP_EV_POWER, .t_ms = app_now_ms(), .arg.u32 = ext_power_changed ? 1 : 0};
    app_post(&ev);
}

esp_err_t app_init(void)
{
    s_queue = xQueueCreate(APP_EVENT_QUEUE_DEPTH, sizeof(app_event_t));
    s_snapshot_lock = xSemaphoreCreateMutex();
    s_setting_applied = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_queue != NULL && s_snapshot_lock != NULL && s_setting_applied != NULL, ESP_ERR_NO_MEM, TAG,
                        "queue");
    s_snapshot.state = APP_STATE_BOOT;
    btn_fsm_init(&s_btn);
    /* The press that woke the device from deep sleep only wakes it. */
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        btn_fsm_arm_swallow(&s_btn, app_now_ms());
    }

    /* Buttons first: in power-save mode the button library installs the GPIO ISR service itself. */
    ESP_RETURN_ON_ERROR(buttons_init(on_button_raw), TAG, "buttons");
    ESP_RETURN_ON_ERROR(feedback_init(on_feedback_step), TAG, "feedback");
    ESP_RETURN_ON_ERROR(power_init(on_power), TAG, "power");
    /* Host tools poll `version` to detect readiness, so it is registered after every other command. */
    ESP_RETURN_ON_ERROR(app_cmds_io_register(), TAG, "io commands");
    return app_cmds_register();
}

esp_err_t app_start(void)
{
    const app_task_cfg_t cfg = APP_TASK_APP;
    const BaseType_t ok = xTaskCreate(app_task, cfg.name, cfg.stack, NULL, cfg.prio, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    const app_task_cfg_t pwr = APP_TASK_PWR;
    ESP_RETURN_ON_ERROR(power_start(pwr.name, pwr.stack, pwr.prio), TAG, "pwr task");

    const esp_timer_create_args_t tick = {
        .callback = tick_cb,
        .name = "app_tick",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick, &s_tick_timer), TAG, "tick timer");
    return esp_timer_start_periodic(s_tick_timer, TICK_PERIOD_US);
}
