#include "app_priv.h"

#include "app.h"
#include "app_tasks.h"
#include "battery.h"
#include "console.h"
#include "display.h"
#include "power.h"
#include "storage.h"
#include "ui.h"
#include "wifi.h"
#include "ws.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "app_cmds";

#define B64_RAW_PER_LINE  57
#define B64_LINE_SIZE     (4 + 76 + 1)

/* Console task only. */
static char s_texts[UI_MAX_TEXTS][UI_TEXT_SIZE];

static console_rc_t send(cJSON *root)
{
    return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
}

static console_rc_t cmd_version(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const esp_app_desc_t *desc = esp_app_get_description();
    char id[APP_DEVICE_ID_SIZE];
    char built[40];
    app_get_device_id(id);
    snprintf(built, sizeof(built), "%s %s", desc->date, desc->time);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "fw", desc->version);
    cJSON_AddStringToObject(root, "idf", esp_get_idf_version());
    cJSON_AddNumberToObject(root, "proto", APP_PROTO_VERSION);
    cJSON_AddStringToObject(root, "deviceId", id);
    cJSON_AddStringToObject(root, "model", APP_MODEL);
    cJSON_AddStringToObject(root, "built", built);
    cJSON_AddStringToObject(root, "reset", app_reset_reason());
    cJSON_AddNumberToObject(root, "uptimeS", app_uptime_s());
    return send(root);
}

static console_rc_t cmd_state(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    app_snapshot_t snap;
    ui_stats_t stats;
    char id[APP_DEVICE_ID_SIZE];
    char url[STORAGE_URL_SIZE];
    storage_url_source_t source;

    app_get_snapshot(&snap);
    ui_get_stats(&stats);
    app_get_device_id(id);
    storage_get_server_url(url, &source);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "fw", esp_app_get_description()->version);
    cJSON_AddNumberToObject(root, "uptimeS", app_uptime_s());
    cJSON_AddStringToObject(root, "reset", app_reset_reason());
    cJSON_AddStringToObject(root, "app", app_state_name(snap.state));
    cJSON_AddStringToObject(root, "screen", ui_current_screen_name());
    cJSON_AddStringToObject(root, "deviceId", id);

    cJSON *heap = cJSON_AddObjectToObject(root, "heap");
    cJSON_AddNumberToObject(heap, "internalFree", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(heap, "internalMin", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(heap, "psramFree", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(heap, "lvglUsedPct", stats.lvgl_used_pct);

    cJSON *ui = cJSON_AddObjectToObject(root, "ui");
    cJSON_AddNumberToObject(ui, "frames", stats.frames);
    cJSON_AddNumberToObject(ui, "frameMsAvg", stats.avg_ms_x10 / 10.0);
    cJSON_AddNumberToObject(ui, "frameMsMax", stats.max_ms_x10 / 10.0);
    cJSON_AddNumberToObject(ui, "flushErrors", display_flush_errors());

    const char *source_name = source == STORAGE_URL_SOURCE_NVS ? "nvs" : "default";
    cJSON_AddStringToObject(root, "nvs", storage_is_degraded() ? "error" : "ok");
    cJSON_AddBoolToObject(root, "paired", snap.link.paired);
    cJSON_AddBoolToObject(root, "wifiConfigured", storage_has_wifi());
    cJSON_AddStringToObject(root, "server", url);
    cJSON_AddStringToObject(root, "serverSource", source_name);
    cJSON_AddBoolToObject(root, "hasBackup", storage_has_backup());
    cJSON_AddStringToObject(root, "mode", proto_mode_name(snap.link.mode));
    cJSON_AddStringToObject(root, "name", snap.link.name);

    wifi_status_t wifi;
    wifi_get_status(&wifi);
    cJSON *link = cJSON_AddObjectToObject(root, "link");
    cJSON_AddStringToObject(link, "wifi", app_wifi_state_name(snap.link.wifi));
    cJSON_AddStringToObject(link, "ssid", snap.link.ssid);
    cJSON_AddStringToObject(link, "ip", wifi.ip);
    cJSON_AddNumberToObject(link, "rssi", wifi.rssi);
    cJSON_AddStringToObject(link, "ws", app_ws_state_name(snap.link.ws));
    cJSON_AddStringToObject(link, "wsPhase", app_ws_phase_name(snap.link.ws_phase));
    cJSON_AddStringToObject(link, "server", snap.link.server);
    cJSON_AddStringToObject(link, "serverSource", source_name);
    cJSON_AddStringToObject(link, "lastError", snap.link.last_error);
    const int32_t retry_ms = snap.link.retry_at_ms != 0 ? (int32_t)(snap.link.retry_at_ms - app_now_ms()) : 0;
    cJSON_AddNumberToObject(link, "retryInS", retry_ms > 0 ? (retry_ms + 999) / 1000 : 0);
    cJSON_AddNumberToObject(link, "rxDropped", ws_rx_dropped());
    cJSON_AddNumberToObject(link, "rxInvalid", snap.link.rx_invalid);
    cJSON_AddNumberToObject(link, "rejected401", snap.link.rejected_401);
    cJSON_AddNumberToObject(root, "eventsDropped", snap.events_dropped);

    cJSON *settings = cJSON_AddObjectToObject(root, "settings");
    for (storage_setting_t s = 0; s < STORAGE_SETTING_COUNT; s++) {
        cJSON_AddNumberToObject(settings, storage_setting_name(s), storage_get_setting(s));
    }

    const uint32_t now = app_now_ms();
    cJSON_AddNumberToObject(root, "idleS", (now - snap.last_user_ms) / 1000);

    power_battery_t batt;
    power_get_battery(&batt);
    cJSON *battery = cJSON_AddObjectToObject(root, "battery");
    cJSON_AddStringToObject(battery, "state", batt.valid ? battery_state_name(batt.state) : "unknown");
    cJSON_AddNumberToObject(battery, "pct", batt.pct);
    cJSON_AddNumberToObject(battery, "mv", batt.mv);
    cJSON_AddBoolToObject(battery, "extPower", batt.ext_power);
    cJSON_AddBoolToObject(battery, "low", batt.low);
    cJSON_AddBoolToObject(battery, "critical", batt.critical);
    cJSON_AddBoolToObject(battery, "injected", batt.injected);
    cJSON_AddStringToObject(battery, "presence", battery_presence_name(batt.presence));

    if (snap.has_last_btn) {
        cJSON *btn = cJSON_AddObjectToObject(root, "lastButton");
        cJSON_AddStringToObject(btn, "id", btn_id_name(snap.last_btn.id));
        cJSON_AddStringToObject(btn, "kind", btn_kind_name(snap.last_btn.kind));
        cJSON_AddBoolToObject(btn, "swallowed", snap.last_btn.swallowed);
        cJSON_AddNumberToObject(btn, "agoMs", now - snap.last_btn_ms);
    } else {
        cJSON_AddNullToObject(root, "lastButton");
    }
    return send(root);
}

static console_rc_t cmd_screen(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const size_t count = ui_get_texts(s_texts, UI_MAX_TEXTS);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", ui_current_screen_name());
    cJSON *texts = cJSON_AddArrayToObject(root, "texts");
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(texts, cJSON_CreateString(s_texts[i]));
    }
    return send(root);
}

static void base64_line(const uint8_t *src, size_t len, char *dst)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
        *dst++ = ALPHABET[v >> 18];
        *dst++ = ALPHABET[(v >> 12) & 0x3F];
        *dst++ = ALPHABET[(v >> 6) & 0x3F];
        *dst++ = ALPHABET[v & 0x3F];
    }
    if (len - i == 1) {
        const uint32_t v = (uint32_t)src[i] << 16;
        *dst++ = ALPHABET[v >> 18];
        *dst++ = ALPHABET[(v >> 12) & 0x3F];
        *dst++ = '=';
        *dst++ = '=';
    } else if (len - i == 2) {
        const uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
        *dst++ = ALPHABET[v >> 18];
        *dst++ = ALPHABET[(v >> 12) & 0x3F];
        *dst++ = ALPHABET[(v >> 6) & 0x3F];
        *dst++ = '=';
    }
    *dst = '\0';
}

static console_rc_t cmd_screenshot(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const uint8_t *pixels = NULL;
    size_t len = 0;
    uint32_t crc = 0;
    const esp_err_t err = display_snapshot(&pixels, &len, &crc);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return console_fail(CONSOLE_ERR_INVALID_STATE, "built without CONFIG_DEVOTCHI_SHADOW_FB");
    }
    if (err != ESP_OK) {
        return console_fail(CONSOLE_ERR_BUSY, "display busy");
    }

    /* The ui lock is free from here on: the transfer reads the snapshot, not the live frame. */
    char line[96];
    snprintf(line, sizeof(line), "@@SCREENSHOT BEGIN w=%d h=%d fmt=RGB565LE enc=base64 bytes=%u crc32=%08lx",
             320, 240, (unsigned)len, (unsigned long)crc);
    if (!console_payload(line)) {
        return CONSOLE_OK;
    }

    char data[B64_LINE_SIZE] = "@@D ";
    unsigned lines = 0;
    for (size_t off = 0; off < len; off += B64_RAW_PER_LINE) {
        const size_t n = len - off < B64_RAW_PER_LINE ? len - off : B64_RAW_PER_LINE;
        base64_line(pixels + off, n, data + 4);
        if (!console_payload(data)) {
            return CONSOLE_OK;
        }
        lines++;
    }
    snprintf(line, sizeof(line), "@@SCREENSHOT END lines=%u", lines);
    console_payload(line);
    return CONSOLE_OK;
}

static void add_task(cJSON *list, const char *name, uint32_t stack)
{
    const TaskHandle_t task = xTaskGetHandle(name);
    if (task == NULL) {
        return;
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", name);
    if (stack != 0) {
        cJSON_AddNumberToObject(item, "stack", stack);
    }
    cJSON_AddNumberToObject(item, "free", uxTaskGetStackHighWaterMark(task));
    cJSON_AddItemToArray(list, item);
}

static console_rc_t cmd_tasks(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const app_task_cfg_t own[] = {APP_TASK_APP, APP_TASK_UI,  APP_TASK_NET,
                                  APP_TASK_WS,  APP_TASK_PWR, APP_TASK_CONSOLE};
    static const char *const SYSTEM[] = {"sys_evt", "esp_timer", "ipc0", "ipc1", "main", "wifi", "tiT"};

    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "tasks");
    for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
        add_task(list, own[i].name, own[i].stack);
    }
    for (size_t i = 0; i < sizeof(SYSTEM) / sizeof(SYSTEM[0]); i++) {
        add_task(list, SYSTEM[i], 0);
    }
    return send(root);
}

static const console_cmd_t CMDS[] = {
    {"state", "application state as one JSON object", cmd_state},
    {"screen", "current screen id and visible texts", cmd_screen},
    {"screenshot", "dump the panel content (RGB565LE, base64, CRC-32)", cmd_screenshot},
    {"tasks", "stack high-water marks in bytes", cmd_tasks},
    /* Last: answering `version` tells host tools that every command exists. */
    {"version", "firmware, IDF, protocol, device id, reset reason, uptime", cmd_version},
};

esp_err_t app_cmds_register(void)
{
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        ESP_RETURN_ON_ERROR(console_register(&CMDS[i]), TAG, "%s", CMDS[i].name);
    }
    return ESP_OK;
}
