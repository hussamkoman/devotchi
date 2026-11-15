#include "app_priv.h"

#include "app.h"
#include "console.h"
#include "display.h"
#include "fb_seq.h"
#include "feedback.h"
#include "pet_builtin.h"
#include "proto.h"
#include "settings_menu.h"
#include "storage.h"
#include "ui.h"
#include "ui_icons.h"
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

_Static_assert(UI_SETTINGS_VALUE_SIZE == SETTINGS_VALUE_TEXT_SIZE, "value text fits the screen row");
_Static_assert(UI_SETTINGS_ROWS >= SETTINGS_LIST_MAX, "every list fits the screen");

/* The NVS key behind each value item (architecture 7.3). */
static const storage_setting_t STORAGE_IDS[SETTINGS_VALUE_COUNT] = {
    [SETTINGS_ITEM_BRIGHTNESS - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_BRIGHTNESS,
    [SETTINGS_ITEM_SCREEN_TIMEOUT - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_DIM_S,
    [SETTINGS_ITEM_DEEP_SLEEP - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_DEEP_MIN,
    [SETTINGS_ITEM_SHOW_CLOCK - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_CLOCK,
    [SETTINGS_ITEM_CLOCK_BRIGHTNESS - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_CLOCK_BRI,
    [SETTINGS_ITEM_H24 - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_H24,
    [SETTINGS_ITEM_NIGHT - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_NIGHT,
    [SETTINGS_ITEM_HAPTICS - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_HAPTIC,
    [SETTINGS_ITEM_LED - SETTINGS_VALUE_FIRST] = STORAGE_SETTING_LED,
};

static const char *const ICONS[SETTINGS_ITEM_COUNT] = {
    [SETTINGS_ITEM_PET] = UI_ICON_PET,           [SETTINGS_ITEM_DISPLAY] = UI_ICON_SUN,
    [SETTINGS_ITEM_CLOCK] = UI_ICON_CLOCK,       [SETTINGS_ITEM_HAPTICS_LED] = UI_ICON_BUZZ,
    [SETTINGS_ITEM_DESK] = UI_ICON_DESK,         [SETTINGS_ITEM_NETWORK] = UI_ICON_WIFI,
    [SETTINGS_ITEM_SHOWCASE] = UI_ICON_PLAY,     [SETTINGS_ITEM_ABOUT] = UI_ICON_INFO,
    [SETTINGS_ITEM_BRIGHTNESS] = UI_ICON_SUN,    [SETTINGS_ITEM_SCREEN_TIMEOUT] = UI_ICON_CLOCK,
    [SETTINGS_ITEM_DEEP_SLEEP] = UI_ICON_MOON,   [SETTINGS_ITEM_SHOW_CLOCK] = UI_ICON_CLOCK,
    [SETTINGS_ITEM_CLOCK_BRIGHTNESS] = UI_ICON_SUN, [SETTINGS_ITEM_H24] = UI_ICON_CLOCK,
    [SETTINGS_ITEM_NIGHT] = UI_ICON_MOON,        [SETTINGS_ITEM_HAPTICS] = UI_ICON_BUZZ,
    [SETTINGS_ITEM_LED] = UI_ICON_BELL,          [SETTINGS_ITEM_WIFI_SETUP] = UI_ICON_WIFI,
    [SETTINGS_ITEM_UNPAIR] = UI_ICON_PLUG,       [SETTINGS_ITEM_FACTORY_RESET] = UI_ICON_RESET,
    [SETTINGS_ITEM_SETTINGS] = UI_ICON_MENU,
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
        .cursor = s_menu.cursor,
        .list = s_menu.list,
        .item = settings_menu_item(&s_menu),
        .view = s_menu.view,
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

static bool desk(void)
{
    return app_link_mode() == PROTO_MODE_DESK;
}

static void load_values(uint16_t values[SETTINGS_VALUE_COUNT])
{
    for (int i = 0; i < SETTINGS_VALUE_COUNT; i++) {
        values[i] = storage_get_setting(STORAGE_IDS[i]);
    }
}

static const char *act_name(settings_act_kind_t kind)
{
    static const char *const NAMES[] = {
        [SETTINGS_ACT_NONE] = "none",       [SETTINGS_ACT_MOVED] = "move",     [SETTINGS_ACT_PREVIEW] = "preview",
        [SETTINGS_ACT_SAVE] = "save",       [SETTINGS_ACT_CANCEL] = "cancel",  [SETTINGS_ACT_DESK] = "desk",
        [SETTINGS_ACT_PROGRESS] = "hold",   [SETTINGS_ACT_NUDGE] = "nudge",    [SETTINGS_ACT_REFUSED] = "refused",
        [SETTINGS_ACT_EXECUTE] = "execute", [SETTINGS_ACT_EXIT] = "exit",
    };
    return kind < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[kind] : "none";
}

static void report(const char *op, uint16_t value)
{
    char evt[160];
    snprintf(evt, sizeof(evt),
             "{\"evt\":\"settings\",\"op\":\"%s\",\"list\":\"%s\",\"row\":\"%s\",\"view\":\"%s\",\"value\":%u}",
             op, settings_list_key(s_menu.list), settings_item_key(settings_menu_item(&s_menu)),
             settings_view_name(s_menu.view), value);
    console_evt(evt);
}

bool app_settings_active(void)
{
    return s_active;
}

void app_settings_open(uint32_t now, settings_list_t list)
{
    uint16_t values[SETTINGS_VALUE_COUNT];
    load_values(values);
    settings_menu_open(&s_menu, list, values, desk(), now);
    s_active = true;
    s_picker = false;
    publish_view();
    report("open", 0);
}

void app_settings_close(void)
{
    if (s_active && s_menu.view == SETTINGS_VIEW_EDITOR) {
        /* An edit that never got its Save leaves the stored value in effect. */
        display_set_brightness((uint8_t)storage_get_setting(STORAGE_SETTING_BRIGHTNESS));
    }
    s_active = false;
    s_picker = false;
    publish_view();
}

void app_settings_to_top(uint32_t now)
{
    if (!s_active) {
        return;
    }
    if (s_menu.view == SETTINGS_VIEW_EDITOR) {
        display_set_brightness((uint8_t)storage_get_setting(STORAGE_SETTING_BRIGHTNESS));
    }
    uint16_t values[SETTINGS_VALUE_COUNT];
    load_values(values);
    settings_menu_open(&s_menu, SETTINGS_LIST_TOP, values, desk(), now);
    s_picker = false;
    publish_view();
}

static void set_mode(uint16_t value, uint32_t now)
{
    if (!app_link_online()) {
        app_toast("Mode needs the server");
        return;
    }
    const proto_mode_t mode = value != 0 ? PROTO_MODE_DESK : PROTO_MODE_ASK;
    char extra[24];
    snprintf(extra, sizeof(extra), ",\"mode\":\"%s\"", proto_mode_name(mode));
    app_link_send(now, "set_mode", s_tx, proto_write_set_mode(s_tx, sizeof(s_tx), mode), extra);
    app_toast_kind(UI_TOAST_DESK, mode == PROTO_MODE_DESK ? "At desk: on" : "At desk: off");
}

/* The editor shows each value as it steps (52); only the backlight has something to preview on this screen. */
static void preview(settings_item_t item, uint16_t value)
{
    if (item == SETTINGS_ITEM_BRIGHTNESS) {
        display_set_brightness((uint8_t)value);
    }
}

static void save(settings_item_t item, uint16_t value)
{
    const storage_setting_t id = STORAGE_IDS[item - SETTINGS_VALUE_FIRST];
    if (storage_set_setting(id, value) != ESP_OK) {
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
        const pet_builtin_t *b = builtin ? pet_builtin_at((unsigned)s_picker_page) : NULL;
        if (b != NULL && b->tagline[0] != '\0') {
            snprintf(s_picker_line, sizeof(s_picker_line), "Built in \xC2\xB7 %s", b->tagline);
        } else if (b != NULL) {
            snprintf(s_picker_line, sizeof(s_picker_line), "Built in");
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

static void execute(settings_item_t item, uint32_t now)
{
    switch (item) {
    case SETTINGS_ITEM_PET:
        picker_open();
        return;
    case SETTINGS_ITEM_SHOWCASE:
        app_flow_showcase_enter(now);
        return;
    case SETTINGS_ITEM_ABOUT:
        app_settings_close();
        app_flow_show_about(now);
        return;
    case SETTINGS_ITEM_WIFI_SETUP:
        if (storage_set_prov_request(true) == ESP_OK) {
            ESP_LOGI(TAG, "restarting into WiFi setup");
            vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
            esp_restart();
        }
        app_toast("Wi-Fi setup needs storage");
        return;
    case SETTINGS_ITEM_UNPAIR:
        app_settings_close();
        app_link_unpair(now);
        app_toast("Unpaired");
        return;
    case SETTINGS_ITEM_FACTORY_RESET:
        app_factory_reset();
        return;
    default:
        return;
    }
}

static void on_act(const settings_act_t *act, uint32_t now)
{
    if (act->kind != SETTINGS_ACT_NONE && act->kind != SETTINGS_ACT_PROGRESS) {
        report(act_name(act->kind), act->value);
    }
    switch (act->kind) {
    case SETTINGS_ACT_NONE:
        break;
    case SETTINGS_ACT_PROGRESS:
        ui_tabs_hold_progress(UI_TAB_APPROVE, act->value);
        break;
    case SETTINGS_ACT_NUDGE:
        feedback_play(FB_SEQ_CLICK);
        ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
        ui_tabs_nudge(UI_TAB_APPROVE, "Hold to erase");
        break;
    case SETTINGS_ACT_REFUSED:
        feedback_play(FB_SEQ_CLICK);
        ui_tabs_shake(UI_TAB_APPROVE);
        break;
    case SETTINGS_ACT_MOVED:
        feedback_play(FB_SEQ_CLICK);
        break;
    case SETTINGS_ACT_PREVIEW:
    case SETTINGS_ACT_CANCEL:
        feedback_play(FB_SEQ_CLICK);
        preview(act->item, act->value);
        break;
    case SETTINGS_ACT_SAVE:
        feedback_play(FB_SEQ_CLICK);
        save(act->item, act->value);
        break;
    case SETTINGS_ACT_DESK:
        feedback_play(FB_SEQ_CLICK);
        set_mode(act->value, now);
        break;
    case SETTINGS_ACT_EXECUTE:
        feedback_play(FB_SEQ_CLICK);
        ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
        execute(act->item, now);
        break;
    case SETTINGS_ACT_EXIT:
        app_settings_close();
        break;
    }
}

void app_settings_on_button(const btn_fsm_out_t *out, uint32_t now)
{
    if (s_picker) {
        if (out->kind == BTN_CLICK) {
            feedback_play(FB_SEQ_CLICK);
        }
        s_menu.last_ms = now;
        picker_button(out, now);
        publish_view();
        return;
    }
    settings_menu_sync(&s_menu, desk(), app_flow_pending_count() > 0);
    const settings_act_t act = settings_menu_button(&s_menu, out, now);
    on_act(&act, now);
    publish_view();
}

btn_mode_t app_settings_btn_mode(btn_id_t id)
{
    return s_picker ? BTN_MODE_CLICK : settings_menu_btn_mode(&s_menu, id);
}

void app_settings_run_deadlines(uint32_t now)
{
    if (!s_active) {
        return;
    }
    if (s_picker) {
        /* The picker shares the list's minute (ux-flows 6). */
        if ((int32_t)(now - (s_menu.last_ms + SETTINGS_LIST_IDLE_MS)) >= 0) {
            app_settings_close();
            report("exit", 0);
        }
        return;
    }
    const settings_act_t act = settings_menu_tick(&s_menu, now);
    if (act.kind != SETTINGS_ACT_NONE) {
        on_act(&act, now);
        publish_view();
    }
}

uint32_t app_settings_deadline(void)
{
    if (!s_active) {
        return 0;
    }
    return s_picker ? s_menu.last_ms + SETTINGS_LIST_IDLE_MS : settings_menu_deadline(&s_menu);
}

void app_settings_on_request(app_request_t *req, uint32_t now)
{
    if (strcmp(req->text, "close") == 0) {
        app_settings_close();
        app_request_complete(true, "");
        return;
    }
    uint16_t values[SETTINGS_VALUE_COUNT];
    load_values(values);
    if (!settings_menu_open_path(&s_menu, req->text, values, desk(), now)) {
        app_request_complete(false, "settings open top|quick|display|clock|haptics_led|network[/<row>]");
        return;
    }
    s_active = true;
    s_picker = false;
    publish_view();
    report("open", 0);
    app_request_complete(true, "");
}

/* The list's row texts: values, the pet's name and the showcase guard. */
static void render_list(ui_settings_t *ui)
{
    size_t count;
    const settings_item_t *items = settings_list_items(s_menu.list, &count);
    ui->count = (uint8_t)count;
    ui->approve = "Open";
    for (size_t i = 0; i < count; i++) {
        const settings_item_t item = items[i];
        ui->icons[i] = ICONS[item];
        ui->labels[i] = settings_item_label(item);
        ui->values[i][0] = '\0';
        switch (settings_item_kind(item)) {
        case SETTINGS_KIND_LIST:
            ui->chevron[i] = true;
            break;
        case SETTINGS_KIND_VALUE:
        case SETTINGS_KIND_TOGGLE:
            settings_value_text(item, settings_menu_value(&s_menu, item), ui->values[i]);
            break;
        case SETTINGS_KIND_ACTION:
        case SETTINGS_KIND_DESTRUCTIVE:
            break;
        }
        if (item == SETTINGS_ITEM_PET) {
            strlcpy(ui->values[i], app_pets_selected(), sizeof(ui->values[i]));
            if (ui->values[i][0] >= 'a' && ui->values[i][0] <= 'z') {
                ui->values[i][0] = (char)(ui->values[i][0] - 'a' + 'A');
            }
        }
        if (item == SETTINGS_ITEM_SHOWCASE && s_menu.showcase_blocked) {
            ui->muted[i] = true;
        }
    }
    const settings_item_t focused = settings_menu_item(&s_menu);
    switch (settings_item_kind(focused)) {
    case SETTINGS_KIND_VALUE:
        ui->approve = "Change";
        break;
    case SETTINGS_KIND_TOGGLE:
        ui->approve = s_menu.desk ? "Turn off" : "Turn on";
        break;
    default:
        break;
    }
    if (focused == SETTINGS_ITEM_SHOWCASE && s_menu.showcase_blocked) {
        ui->desc = "A request is waiting";
    }
    if (s_menu.list == SETTINGS_LIST_QUICK) {
        ui->note = "Desk mode sends every request straight to your terminal. Showcase is a demo for visitors.";
    }
}

static void render_editor(ui_settings_t *ui)
{
    const settings_item_t item = settings_menu_item(&s_menu);
    const uint16_t value = settings_menu_value(&s_menu, item);
    ui->title = settings_item_label(item);
    settings_value_parts(item, value, ui->big, ui->unit);
    size_t count;
    const uint16_t *choices = settings_item_choices(item, &count);
    ui->steps = (uint8_t)count;
    size_t len = 0;
    for (size_t i = 0; i < count; i++) {
        if (choices[i] == value) {
            ui->step = (uint8_t)i;
        }
        char text[SETTINGS_VALUE_TEXT_SIZE];
        char unit[SETTINGS_VALUE_TEXT_SIZE];
        settings_value_parts(item, choices[i], text, unit);
        len += (size_t)snprintf(ui->choices + len, len < sizeof(ui->choices) ? sizeof(ui->choices) - len : 0,
                                i > 0 ? " \xC2\xB7 %s" : "%s", text);
    }
    ui->hint = item == SETTINGS_ITEM_BRIGHTNESS ? "The screen shows each value as you step."
                                                : "Save keeps it, Cancel brings the old value back.";
}

static void render_confirm(ui_settings_t *ui)
{
    const bool reset = settings_menu_item(&s_menu) == SETTINGS_ITEM_FACTORY_RESET;
    ui->confirm_title = reset ? "Factory reset?" : "Unpair?";
    ui->confirm_text = reset ? "Erases Wi-Fi, pairing and settings, then restarts into setup."
                             : "Forgets this server. The device shows a new pairing code.";
    ui->confirm_hold = reset ? "Erase" : "Unpair";
}

void app_settings_render(bool preview_only)
{
    /* Static: the screen data is too large for the app task's stack. */
    static ui_settings_t ui;
    if (!s_active) {
        if (!preview_only) {
            return;
        }
        uint16_t values[SETTINGS_VALUE_COUNT];
        load_values(values);
        settings_menu_open(&s_menu, SETTINGS_LIST_TOP, values, desk(), 0);
    }
    if (s_picker && !preview_only) {
        ui_picker_t page;
        picker_page(&page);
        ui_picker_set(&page);
        return;
    }
    settings_menu_sync(&s_menu, desk(), app_flow_pending_count() > 0);
    memset(&ui, 0, sizeof(ui));
    ui.cursor = s_menu.cursor;
    ui.title = settings_list_title(s_menu.list);
    switch (s_menu.view) {
    case SETTINGS_VIEW_EDITOR:
        ui.view = UI_SETTINGS_EDITOR;
        render_editor(&ui);
        break;
    case SETTINGS_VIEW_CONFIRM:
        ui.view = UI_SETTINGS_CONFIRM;
        render_confirm(&ui);
        break;
    case SETTINGS_VIEW_LIST:
        ui.view = UI_SETTINGS_LIST;
        render_list(&ui);
        break;
    }
    ui_settings_set(&ui);
}

void app_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset");
    /* Stored pets go key by key: the pets partition is erased whole only when it cannot be used. */
    static storage_pet_entry_t pets[PET_STORE_SLOTS];
    const size_t n = storage_pets_list(pets, PET_STORE_SLOTS);
    for (size_t i = 0; i < n; i++) {
        storage_pets_remove(pets[i].id);
    }
    const esp_err_t err = storage_erase_all();
    const esp_err_t wifi_err = wifi_restore();
    if (err != ESP_OK || wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "factory reset incomplete: storage %s, wifi %s", esp_err_to_name(err),
                 esp_err_to_name(wifi_err));
    }
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
}
