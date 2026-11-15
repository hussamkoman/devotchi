#include "app_priv.h"

#include "app.h"
#include "app_tasks.h"
#include "battery.h"
#include "console.h"
#include "display.h"
#include "power.h"
#include "settings_menu.h"
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

#define REQUEST_TIMEOUT_MS  3000

/* Console task only. */
static char s_texts[UI_MAX_TEXTS][UI_TEXT_SIZE];
static char s_toast[UI_TEXT_SIZE];
static char s_banner[UI_TEXT_SIZE];
static char s_strip[UI_TEXT_SIZE];
static app_flow_view_t s_view;
static app_snapshot_t s_snap;

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
    app_pets_view_t pets;
    app_pets_get_view(&pets);
    static const char *const SOURCES[] = {[PET_SOURCE_BUILTIN] = "builtin", [PET_SOURCE_STORED] = "stored",
                                          [PET_SOURCE_FALLBACK] = "fallback"};
    cJSON *pet = cJSON_AddObjectToObject(root, "pet");
    cJSON_AddStringToObject(pet, "selected", pets.selected);
    cJSON_AddStringToObject(pet, "shown", pets.shown);
    cJSON_AddStringToObject(pet, "source", (size_t)pets.source < 3 ? SOURCES[pets.source] : "fallback");
    cJSON *store = cJSON_AddObjectToObject(root, "pets");
    cJSON_AddStringToObject(store, "partition", pet_part_state_name(pets.partition));
    cJSON_AddNumberToObject(store, "slots", (double)pets.slots);
    cJSON_AddNumberToObject(store, "free", (double)(pets.slots > pets.stored ? pets.slots - pets.stored : 0));
    cJSON_AddBoolToObject(store, "fetching", pets.fetching);
    cJSON *list = cJSON_AddArrayToObject(store, "stored");
    for (size_t i = 0; i < pets.stored && i < PET_STORE_SLOTS; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", pets.entries[i].id);
        cJSON_AddNumberToObject(e, "rev", pets.entries[i].rev);
        cJSON_AddItemToArray(list, e);
    }
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
    cJSON_AddNumberToObject(root, "evtDropped", console_evt_dropped());

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

    power_pm_state_t pm;
    power_pm_get(&pm);
    cJSON *pw = cJSON_AddObjectToObject(root, "power");
    cJSON_AddStringToObject(pw, "stage", power_stage_name(app_power_stage()));
    cJSON_AddBoolToObject(pw, "panelAsleep", display_is_asleep());
    cJSON_AddBoolToObject(pw, "uiParked", !ui_is_running());
    cJSON_AddBoolToObject(pw, "cpuLock", pm.cpu_lock);
    cJSON_AddNumberToObject(pw, "cpuMhz", pm.cpu_mhz);
    cJSON_AddBoolToObject(pw, "clock", app_power_clock_shown());
    uint32_t sleep_ms;
    cJSON_AddBoolToObject(pw, "sleepWarning", app_power_sleep_warning(&sleep_ms));

    cJSON *clk = cJSON_AddObjectToObject(root, "clock");
    cJSON_AddBoolToObject(clk, "valid", app_clock_valid());
    cJSON_AddNumberToObject(clk, "epoch", (double)(app_clock_epoch_ms() / 1000));
    cJSON_AddNumberToObject(clk, "offsetMin", app_clock_offset_min());
    cJSON_AddBoolToObject(clk, "night", app_clock_night());
    char time[CLOCK_TIME_SIZE];
    char date[CLOCK_DATE_SIZE];
    app_clock_format(app_clock_epoch_ms(), time, date);
    cJSON_AddStringToObject(clk, "time", time);
    cJSON_AddStringToObject(clk, "date", date);

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

/* "tabs": one entry per button with its state and text, plus "any" for the single centred tab. */
static void add_tabs(cJSON *root)
{
    ui_tab_info_t tabs[UI_TAB_COUNT];
    const bool any = ui_tabs_get(tabs);
    cJSON *obj = cJSON_AddObjectToObject(root, "tabs");
    if (any) {
        cJSON_AddStringToObject(obj, "any", tabs[UI_TAB_ACTION].text);
        return;
    }
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (tabs[i].state == UI_TAB_NONE) {
            cJSON_AddNullToObject(obj, ui_tab_btn_name((ui_tab_btn_t)i));
            continue;
        }
        cJSON *tab = cJSON_AddObjectToObject(obj, ui_tab_btn_name((ui_tab_btn_t)i));
        cJSON_AddStringToObject(tab, "state", ui_tab_state_name(tabs[i].state));
        cJSON_AddStringToObject(tab, "text", tabs[i].text);
    }
}

static console_rc_t cmd_ui(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc >= 3 && strcmp(argv[1], "demo") == 0) {
        if (strcmp(argv[2], "off") == 0) {
            ui_demo_off();
            /* The screens drew sample data; the app draws its own state again. */
            bool ok;
            char message[APP_REQUEST_MESSAGE_SIZE];
            if (app_request(APP_REQUEST_RENDER, "", NULL, REQUEST_TIMEOUT_MS, &ok, message) != APP_REQUEST_DONE) {
                return console_fail(CONSOLE_ERR_BUSY, "demo off, but the app task did not redraw");
            }
            return CONSOLE_OK;
        }
        const esp_err_t err = ui_demo(argv[2], argc >= 4 ? argv[3] : NULL);
        if (err == ESP_ERR_NOT_FOUND) {
            return console_fail(CONSOLE_ERR_BAD_ARGS, "ui demo %s", ui_demo_usage());
        }
        return err == ESP_OK ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "%s", esp_err_to_name(err));
    }
    if (argc == 2 && strcmp(argv[1], "tabs") == 0) {
        cJSON *root = cJSON_CreateObject();
        add_tabs(root);
        return send(root);
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "ui demo <name> [variant] | ui demo off | ui tabs");
}

static console_rc_t cmd_screen(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const ui_screen_id_t screen = ui_current_screen();
    const size_t count = ui_get_texts(s_texts, UI_MAX_TEXTS);
    ui_status_t bar;
    ui_status_get(&bar);
    ui_overlay_get(s_toast, s_banner, s_strip);
    app_flow_get_view(&s_view);
    app_get_snapshot(&s_snap);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", ui_screen_name(screen));
    const char *page = "main";
    if (screen == UI_SCREEN_PET) {
        page = app_flow_page_name(s_view.page);
    }
    cJSON_AddStringToObject(root, "page", page);
    cJSON_AddBoolToObject(root, "preview", s_snap.link.preview);
    cJSON *texts = cJSON_AddArrayToObject(root, "texts");
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(texts, cJSON_CreateString(s_texts[i]));
    }
    cJSON_AddStringToObject(root, "mood", proto_mood_name(s_view.mood));
    cJSON_AddBoolToObject(root, "demo", ui_demo_is_active());
    add_tabs(root);

    const bool bar_shown = screen != UI_SCREEN_SPLASH && screen != UI_SCREEN_LOWBATT && screen != UI_SCREEN_IDENTIFY &&
                           screen != UI_SCREEN_CLOCK;
    cJSON *sb = cJSON_AddObjectToObject(root, "statusBar");
    cJSON_AddBoolToObject(sb, "visible", bar_shown);
    cJSON *battery = cJSON_AddObjectToObject(sb, "battery");
    cJSON_AddStringToObject(battery, "state", ui_batt_name(bar.battery));
    if (bar.battery == UI_BATT_ABSENT) {
        cJSON_AddNullToObject(battery, "pct");
    } else {
        cJSON_AddNumberToObject(battery, "pct", bar.pct);
    }
    cJSON_AddBoolToObject(battery, "low", bar.low);
    cJSON_AddStringToObject(sb, "link", ui_link_name(bar.link));
    cJSON_AddStringToObject(sb, "mode", bar.desk ? "desk" : "ask");
    cJSON_AddStringToObject(sb, "time", bar.time);
    cJSON_AddNumberToObject(sb, "rssiBars", bar.rssi_bars);

    if ((screen == UI_SCREEN_APPROVAL || screen == UI_SCREEN_QUESTION || screen == UI_SCREEN_PLAN) &&
        s_view.has_request) {
        static const char *const RISK[] = {[PROTO_RISK_LOW] = "low", [PROTO_RISK_MED] = "med", [PROTO_RISK_HIGH] = "high"};
        cJSON *req = cJSON_AddObjectToObject(root, "request");
        cJSON_AddStringToObject(req, "id", s_view.request.id);
        cJSON_AddStringToObject(req, "tool", s_view.request.tool);
        cJSON_AddStringToObject(req, "summary", s_view.request.summary);
        cJSON_AddStringToObject(req, "risk", RISK[s_view.request.risk]);
        cJSON_AddNumberToObject(req, "position", s_view.request.position);
        cJSON_AddNumberToObject(req, "count", s_view.request.count);
        cJSON_AddNumberToObject(req, "page", s_view.request.page);
        cJSON_AddNumberToObject(req, "pages", s_view.request.pages);
        const int32_t left = (int32_t)(s_view.request.deadline_ms - app_now_ms());
        cJSON_AddNumberToObject(req, "remainingMs", left > 0 ? left : 0);
        cJSON_AddBoolToObject(req, "sent", s_view.request.answered);
        cJSON_AddNumberToObject(req, "seen", s_view.request.seen);
        cJSON_AddBoolToObject(req, "armed", s_view.request.armed);
        cJSON_AddBoolToObject(req, "cut", s_view.request.cut);
        cJSON_AddBoolToObject(req, "decide", s_view.request.decide);
        static const char *const ALLOW[] = {[CARD_ALLOW_NONE] = "none", [CARD_ALLOW_INACTIVE] = "inactive",
                                            [CARD_ALLOW_CLICK] = "click", [CARD_ALLOW_HOLD] = "hold"};
        cJSON_AddStringToObject(req, "allow", ALLOW[s_view.request.allow]);
        cJSON_AddStringToObject(req, "session", card_session_name(s_view.request.session));
        cJSON_AddStringToObject(req, "view", card_view_name(s_view.request.view));
        static const char *const KIND[] = {[REQQ_PERMISSION] = "permission", [REQQ_QUESTION] = "question",
                                           [REQQ_PLAN] = "plan"};
        cJSON_AddStringToObject(req, "kind", KIND[s_view.request.kind]);
        if (s_view.request.kind == REQQ_QUESTION) {
            cJSON_AddNumberToObject(req, "question", s_view.request.question);
            cJSON_AddNumberToObject(req, "questions", s_view.request.questions);
            cJSON_AddNumberToObject(req, "options", s_view.request.options);
            cJSON_AddBoolToObject(req, "multi", s_view.request.multi);
            cJSON_AddNumberToObject(req, "opt", s_view.request.opt);
            cJSON_AddNumberToObject(req, "mask", s_view.request.mask);
            cJSON_AddNumberToObject(req, "done", s_view.request.done);
        } else if (s_view.request.kind == REQQ_PLAN) {
            cJSON_AddNumberToObject(req, "steps", s_view.request.steps);
            cJSON_AddNumberToObject(req, "more", s_view.request.more);
        }
        cJSON *rows = cJSON_AddArrayToObject(req, "rows");
        for (uint8_t i = 0; i < s_view.request.rows && i < CARD_MENU_ROWS; i++) {
            cJSON_AddItemToArray(rows, cJSON_CreateString(card_row_name(s_view.request.row[i])));
        }
        cJSON_AddNumberToObject(req, "cursor", s_view.request.cursor);
    } else {
        cJSON_AddNullToObject(root, "request");
    }
    app_settings_view_t settings;
    app_settings_get_view(&settings);
    if (screen == UI_SCREEN_SETTINGS) {
        cJSON *set = cJSON_AddObjectToObject(root, "settings");
        cJSON_AddBoolToObject(set, "active", settings.active);
        cJSON_AddNumberToObject(set, "cursor", settings.cursor);
        cJSON_AddStringToObject(set, "list", settings_list_key(settings.list));
        cJSON_AddStringToObject(set, "row", settings_item_key(settings.item));
        cJSON_AddStringToObject(set, "view", settings_view_name(settings.view));
    }
    cJSON_AddStringToObject(root, "mark", ui_mark_name(ui_mark_get()));
    cJSON_AddBoolToObject(root, "showcase", app_flow_showcase_active());
    if (screen == UI_SCREEN_CLOCK) {
        ui_clock_t c;
        char time[8];
        char date[16];
        ui_clock_get(&c, time, date);
        cJSON *clk = cJSON_AddObjectToObject(root, "clock");
        cJSON_AddStringToObject(clk, "time", time);
        cJSON_AddStringToObject(clk, "date", date);
        cJSON_AddBoolToObject(clk, "night", c.night);
    }
    if (screen == UI_SCREEN_PICKER) {
        cJSON *pk = cJSON_AddObjectToObject(root, "picker");
        cJSON_AddBoolToObject(pk, "open", settings.picker);
        cJSON_AddNumberToObject(pk, "page", settings.page);
    }
    if (s_toast[0] != '\0') {
        cJSON_AddStringToObject(root, "toast", s_toast);
    } else {
        cJSON_AddNullToObject(root, "toast");
    }
    if (s_banner[0] != '\0') {
        cJSON_AddStringToObject(root, "banner", s_banner);
    } else {
        cJSON_AddNullToObject(root, "banner");
    }
    if (s_strip[0] != '\0') {
        cJSON_AddStringToObject(root, "strip", s_strip);
    } else {
        cJSON_AddNullToObject(root, "strip");
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
    {"screen", "current screen id, visible texts and button tabs", cmd_screen},
    {"ui", "ui demo <name> [variant] | ui demo off: sample screens without touching app state; ui tabs: the button tabs", cmd_ui},
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
