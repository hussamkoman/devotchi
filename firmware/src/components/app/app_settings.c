#include "app_priv.h"

#include "app.h"
#include "console.h"
#include "fb_seq.h"
#include "feedback.h"
#include "proto.h"
#include "settings_menu.h"
#include "storage.h"
#include "ui.h"
#include "wifi.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "settings";

#define TX_SIZE           64
/* Lets the console print the command's reply before the restart cuts the USB link. */
#define RESTART_DELAY_MS  200

_Static_assert(UI_SETTINGS_ROWS == SETTINGS_ROW_COUNT, "one screen row per menu row");
_Static_assert(UI_SETTINGS_VALUE_SIZE == SETTINGS_VALUE_TEXT_SIZE, "value text fits the screen row");

/* STORAGE_SETTING_COUNT marks the mode row, which the server owns. */
static const storage_setting_t STORAGE_IDS[SETTINGS_VALUE_ROWS] = {
    [SETTINGS_ROW_BRIGHTNESS] = STORAGE_SETTING_BRIGHTNESS,
    [SETTINGS_ROW_SCREEN_TIMEOUT] = STORAGE_SETTING_DIM_S,
    [SETTINGS_ROW_DEEP_SLEEP] = STORAGE_SETTING_DEEP_MIN,
    [SETTINGS_ROW_HAPTICS] = STORAGE_SETTING_HAPTIC,
    [SETTINGS_ROW_LED] = STORAGE_SETTING_LED,
    [SETTINGS_ROW_MODE] = STORAGE_SETTING_COUNT,
};

/* App task only, except s_view (guarded by s_view_mux). */
static settings_menu_t s_menu;
static bool s_active;
/* The pet picker (60-62): pages are the built-ins, the stored pets and "More pets". */
static bool s_picker;
static size_t s_picker_page;
static char s_picker_line[48];
static char s_tx[TX_SIZE];
static portMUX_TYPE s_view_mux = portMUX_INITIALIZER_UNLOCKED;
static app_settings_view_t s_view;

static void publish_view(void)
{
    portENTER_CRITICAL(&s_view_mux);
    s_view = (app_settings_view_t){
        .active = s_active,
        .cursor = (uint8_t)s_menu.cursor,
        .confirming = s_menu.confirming,
        .picker = s_picker,
        .page = (uint8_t)s_picker_page,
    };
    portEXIT_CRITICAL(&s_view_mux);
}

void app_settings_get_view(app_settings_view_t *out)
{
    portENTER_CRITICAL(&s_view_mux);
    *out = s_view;
    portEXIT_CRITICAL(&s_view_mux);
}

static uint16_t mode_value(void)
{
    return app_link_mode() == PROTO_MODE_DESK ? SETTINGS_MODE_DESK : SETTINGS_MODE_ASK;
}

static void load(void)
{
    uint16_t values[SETTINGS_VALUE_ROWS];
    for (int r = 0; r < SETTINGS_VALUE_ROWS; r++) {
        values[r] = STORAGE_IDS[r] < STORAGE_SETTING_COUNT ? storage_get_setting(STORAGE_IDS[r]) : mode_value();
    }
    settings_menu_open(&s_menu, values);
}

static const char *act_name(settings_act_kind_t kind)
{
    switch (kind) {
    case SETTINGS_ACT_NONE:
        return "none";
    case SETTINGS_ACT_MOVED:
        return "move";
    case SETTINGS_ACT_VALUE:
        return "value";
    case SETTINGS_ACT_CONFIRM_ASK:
        return "confirm";
    case SETTINGS_ACT_CONFIRM_CANCELLED:
        return "cancel";
    case SETTINGS_ACT_EXECUTE:
        return "execute";
    case SETTINGS_ACT_EXIT:
        return "exit";
    }
    return "none";
}

static void report(const char *op, settings_row_t row, uint16_t value)
{
    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"settings\",\"op\":\"%s\",\"row\":\"%s\",\"value\":%u}", op,
             settings_menu_key(row), value);
    console_evt(evt);
}

bool app_settings_active(void)
{
    return s_active;
}

void app_settings_open(uint32_t now)
{
    (void)now;
    load();
    s_active = true;
    publish_view();
    report("open", s_menu.cursor, 0);
}

static void close_menu(void)
{
    s_active = false;
    s_picker = false;
    s_menu.confirming = false;
    publish_view();
}

static void set_mode(uint16_t value, uint32_t now)
{
    if (!app_link_online()) {
        app_toast("Mode needs the server");
        settings_menu_sync(&s_menu, SETTINGS_ROW_MODE, mode_value());
        return;
    }
    const proto_mode_t mode = value == SETTINGS_MODE_DESK ? PROTO_MODE_DESK : PROTO_MODE_ASK;
    char extra[24];
    snprintf(extra, sizeof(extra), ",\"mode\":\"%s\"", proto_mode_name(mode));
    app_link_send(now, "set_mode", s_tx, proto_write_set_mode(s_tx, sizeof(s_tx), mode), extra);
    app_toast_kind(UI_TOAST_DESK, mode == PROTO_MODE_DESK ? "At desk: on" : "At desk: off");
}

static void set_value(settings_row_t row, uint16_t value, uint32_t now)
{
    if (row == SETTINGS_ROW_MODE) {
        set_mode(value, now);
        return;
    }
    const storage_setting_t id = STORAGE_IDS[row];
    const esp_err_t err = storage_set_setting(id, value);
    if (err != ESP_OK) {
        /* Degraded storage: the value applies until the next reset. */
        app_toast("Not saved");
    }
    app_apply_setting(id);
}

bool app_settings_picker(void)
{
    return s_active && s_picker;
}

static void picker_report(const char *op, const char *pet)
{
    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"picker\",\"op\":\"%s\",\"page\":%u,\"pet\":\"%s\"}", op,
             (unsigned)s_picker_page, pet != NULL ? pet : "");
    console_evt(evt);
}

static void picker_open(void)
{
    s_picker = true;
    s_picker_page = 0;
    const size_t n = app_pets_count();
    for (size_t i = 0; i < n; i++) {
        bool builtin;
        const pet_def_t *def = app_pets_load(i, &builtin);
        if (def != NULL && strcmp(def->id, app_pets_selected()) == 0) {
            s_picker_page = i;
        }
    }
    picker_report("open", app_pets_selected());
}

/* What the page shows; NULL def is the "More pets" page. */
static const pet_def_t *picker_page(ui_picker_t *out)
{
    const size_t n = app_pets_count();
    if (s_picker_page > n) {
        s_picker_page = 0;
    }
    *out = (ui_picker_t){.page = (uint8_t)s_picker_page, .pages = (uint8_t)(n + 1)};
    bool builtin = false;
    const pet_def_t *def = s_picker_page < n ? app_pets_load(s_picker_page, &builtin) : NULL;
    if (def != NULL) {
        if (strcmp(def->id, "devo") == 0) {
            snprintf(s_picker_line, sizeof(s_picker_line), "Built in \xC2\xB7 changes colour with its mood");
        } else if (strcmp(def->id, "ember") == 0) {
            snprintf(s_picker_line, sizeof(s_picker_line), "Built in \xC2\xB7 a small jade dragon");
        } else {
            snprintf(s_picker_line, sizeof(s_picker_line), "From the dashboard \xC2\xB7 rev %u", def->rev);
        }
        out->def = def;
        out->line = s_picker_line;
        out->in_use = strcmp(def->id, app_pets_selected()) == 0;
        out->can_use = !out->in_use;
    }
    return def;
}

static void picker_button(const btn_fsm_out_t *out, uint32_t now)
{
    if (out->kind != BTN_CLICK) {
        return;
    }
    ui_picker_t page;
    const pet_def_t *def = picker_page(&page);
    switch (out->id) {
    case BTN_REJECT:
        s_picker = false;
        picker_report("back", NULL);
        break;
    case BTN_ACTION:
        s_picker_page = (s_picker_page + 1) % page.pages;
        picker_report("next", NULL);
        break;
    case BTN_APPROVE:
        if (def != NULL && page.can_use) {
            char id[STORAGE_PET_ID_SIZE];
            strlcpy(id, def->id, sizeof(id));
            const pet_result_t r = app_pets_select(id, now);
            picker_report(r == PET_RESULT_OK ? "use" : "use-failed", id);
            if (r != PET_RESULT_OK) {
                app_toast("Pet not saved");
            }
        }
        break;
    default:
        break;
    }
}

static void execute(settings_row_t row, uint32_t now)
{
    switch (row) {
    case SETTINGS_ROW_PET:
        picker_open();
        return;
    case SETTINGS_ROW_WIFI_SETUP:
        if (storage_set_prov_request(true) == ESP_OK) {
            ESP_LOGI(TAG, "restarting into WiFi setup");
            vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
            esp_restart();
        }
        app_toast("WiFi setup needs storage");
        return;
    case SETTINGS_ROW_UNPAIR:
        close_menu();
        app_link_unpair(now);
        app_toast("Unpaired");
        return;
    case SETTINGS_ROW_FACTORY_RESET:
        app_factory_reset();
        return;
    case SETTINGS_ROW_ABOUT:
        close_menu();
        app_flow_show_about(now);
        return;
    case SETTINGS_ROW_BRIGHTNESS:
    case SETTINGS_ROW_SCREEN_TIMEOUT:
    case SETTINGS_ROW_DEEP_SLEEP:
    case SETTINGS_ROW_HAPTICS:
    case SETTINGS_ROW_LED:
    case SETTINGS_ROW_MODE:
    case SETTINGS_ROW_COUNT:
        return;
    }
}

void app_settings_on_button(const btn_fsm_out_t *out, uint32_t now)
{
    if (s_picker) {
        if (out->kind == BTN_CLICK) {
            feedback_play(FB_SEQ_CLICK);
        }
        picker_button(out, now);
        publish_view();
        return;
    }
    const settings_act_t act = settings_menu_button(&s_menu, out, now);
    if (act.kind != SETTINGS_ACT_NONE) {
        report(act_name(act.kind), act.row, act.value);
    }
    switch (act.kind) {
    case SETTINGS_ACT_NONE:
    case SETTINGS_ACT_MOVED:
    case SETTINGS_ACT_CONFIRM_CANCELLED:
        break;
    case SETTINGS_ACT_VALUE:
        feedback_play(FB_SEQ_CLICK);
        set_value(act.row, act.value, now);
        break;
    case SETTINGS_ACT_CONFIRM_ASK:
        feedback_play(FB_SEQ_CLICK);
        break;
    case SETTINGS_ACT_EXECUTE:
        feedback_play(FB_SEQ_CLICK);
        execute(act.row, now);
        break;
    case SETTINGS_ACT_EXIT:
        close_menu();
        break;
    }
    publish_view();
}

void app_settings_run_deadlines(uint32_t now)
{
    if (!s_active) {
        return;
    }
    const settings_act_t act = settings_menu_tick(&s_menu, now);
    if (act.kind != SETTINGS_ACT_NONE) {
        report(act_name(act.kind), act.row, 0);
        publish_view();
    }
}

uint32_t app_settings_deadline(void)
{
    return s_active && s_menu.confirming ? s_menu.confirm_until : 0;
}

/* What ACTION does on row, for its tab: a value row steps its value, the others open or ask to confirm. */
static const char *action_label(settings_row_t row)
{
    switch (row) {
    case SETTINGS_ROW_PET:
        return "Open";
    case SETTINGS_ROW_WIFI_SETUP:
        return "Set up";
    case SETTINGS_ROW_UNPAIR:
        return "Unpair";
    case SETTINGS_ROW_FACTORY_RESET:
        return "Erase";
    case SETTINGS_ROW_ABOUT:
        return "Open";
    case SETTINGS_ROW_BRIGHTNESS:
    case SETTINGS_ROW_SCREEN_TIMEOUT:
    case SETTINGS_ROW_DEEP_SLEEP:
    case SETTINGS_ROW_HAPTICS:
    case SETTINGS_ROW_LED:
    case SETTINGS_ROW_MODE:
    case SETTINGS_ROW_COUNT:
        break;
    }
    return "Change";
}

void app_settings_render(bool preview)
{
    ui_settings_t ui;
    if (!s_active) {
        if (!preview) {
            return;
        }
        load();
    }
    if (s_picker && !preview) {
        ui_picker_t page;
        picker_page(&page);
        ui_picker_set(&page);
        return;
    }
    settings_menu_sync(&s_menu, SETTINGS_ROW_MODE, mode_value());
    for (int r = 0; r < SETTINGS_ROW_COUNT; r++) {
        ui.labels[r] = settings_menu_label((settings_row_t)r);
        settings_menu_value_text((settings_row_t)r, r < SETTINGS_VALUE_ROWS ? s_menu.values[r] : 0, ui.values[r]);
    }
    ui.cursor = (uint8_t)s_menu.cursor;
    ui.confirming = s_menu.confirming;
    ui.action = action_label(s_menu.cursor);
    ui_settings_set(&ui);
}

void app_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset");
    const esp_err_t err = storage_erase_all();
    const esp_err_t wifi_err = wifi_restore();
    if (err != ESP_OK || wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "factory reset incomplete: storage %s, wifi %s", esp_err_to_name(err),
                 esp_err_to_name(wifi_err));
    }
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
}
