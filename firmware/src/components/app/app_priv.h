#pragma once

#include "app.h"
#include "clock.h"
#include "net_worker.h"
#include "power_fsm.h"
#include "settings_menu.h"
#include "storage.h"
#include "ui.h"

#include "esp_err.h"

#include <stdbool.h>

typedef struct app_request app_request_t;

esp_err_t app_cmds_register(void);

/**
 * Tells the app task that a setting was written and waits until it applied it;
 * false when the queue is full or the app task did not get to it in time.
 * One caller at a time (the console task), never the app task itself.
 */
bool app_setting_changed(storage_setting_t id);

/** App task: brings a written setting into effect (brightness, feedback, battery detection). */
void app_apply_setting(storage_setting_t id);

/** Commands for buttons, feedback, battery and settings. */
esp_err_t app_cmds_io_register(void);

/** Commands for the network link: server, pair, wifi, msg, preview and the pairing backup. */
esp_err_t app_cmds_net_register(void);

/* ---- app.c services for the link state machine (app task only) ---- */

/** Changes the application state and reports it as `@@EVT {"evt":"app",...}`. */
void app_set_state(app_state_t state);

/** Copies the link part of the snapshot and reports wifi/ws changes as `@@EVT {"evt":"link",...}`. */
void app_publish_link(const app_link_info_t *info);

void app_show_screen(ui_screen_id_t id);

/* ---- app_link.c: WiFi, pairing and the WebSocket link (protocol sections 3 and 9) ---- */

/** Initialises the net component. WiFi failing to initialise is a state, not an error. */
esp_err_t app_link_init(void);

/** App task, once: decides between provisioning and connecting. */
void app_link_start(void);

/** App task: handles the link events of app_event_id_t; false for an event that is not one. */
bool app_link_handle_event(const app_event_t *ev);

/** App task: a button output that was not swallowed. */
void app_link_on_button(const btn_fsm_out_t *out);

/** App task: the mode a press of id gets on the screen shown now (btn_fsm latches it at DOWN). */
btn_mode_t app_link_btn_mode(btn_id_t id, uint32_t now);

/** PROTO_CAP_* the server listed in its register_ack (0 for a phase-1 server). */
uint32_t app_link_server_caps(void);

/** The last render drew a flow screen (pet, approval, identify). */
bool app_link_flow_shown(void);

/* ---- app.c: the button machine ---- */

/** App task: swallows presses until every button is up and delay_ms passed (btn_fsm_arm). */
void app_btn_arm(uint32_t now, uint32_t delay_ms);
/** App task: no arming delay runs. */
bool app_btn_armed(uint32_t now);
/** App task: when the arming delay ends if no button goes down; false when none runs or a button is down. */
bool app_btn_arm_end(uint32_t *out);

/** App task: hands a frame to the socket and reports `@@EVT {"evt":"tx",...}`; extra is appended to the event. */
bool app_link_send(uint32_t now, const char *type, const char *frame, int len, const char *extra);

proto_mode_t app_link_mode(void);

/** Device name the server assigned ("" before the first registration). */
const char *app_link_name(void);

/** True while the socket is registered, so a frame sent now reaches the server. */
bool app_link_online(void);

/** App task: Unpair from Settings: erases token and room id (backup rule), stops the socket, starts pairing. */
void app_link_unpair(uint32_t now);
/** App task: renders the current screen now (the power stages pause rendering while the panel sleeps). */
void app_link_render(uint32_t now);
/** App task, before deep sleep: stops the socket and makes no further WiFi, socket or HTTP attempts. */
void app_link_suspend(uint32_t now);
/** True while no WebSocket client exists. */
bool app_link_ws_idle(void);
/**
 * App task, before deep sleep: keeps handling link events until no WebSocket
 * client exists or timeout_ms passed (false); other events are dropped.
 */
bool app_wait_link_idle(uint32_t timeout_ms);

/* ---- app.c: activity hooks for the power stages (architecture 4.8) ---- */

typedef struct {
    app_state_t state;
    uint32_t last_user_ms;
    uint32_t last_agent_ms;
} app_activity_t;

/** Any task: the part of the snapshot the power stages need. */
void app_get_activity(app_activity_t *out);

/** Something the user should see happened: resets the idle timers. */
void app_user_activity(uint32_t now);

/** Agent traffic: postpones deep sleep without waking the screen. */
void app_agent_activity(uint32_t now);

/* ---- app_power.c: power stages, sleep and the `power` command (architecture 4.8) ---- */
/** Reads the power settings and registers the `power` console command. */
esp_err_t app_power_init(void);
/**
 * App task, once before the link starts: after a battery-protection sleep,
 * decides from a fresh battery reading whether to sleep again (may not return).
 */
void app_power_boot_check(void);
/** App task, after every event: evaluates power_fsm and applies the stage, locks and sleep decisions. */
void app_power_update(uint32_t now);
/** The stage applied last. */
power_stage_t app_power_stage(void);
bool app_power_screen_off(void);
/** The lowbatt card owns the screen. */
bool app_power_lowbatt(void);
/** App task: a setting changed; the screen timeout, deep-sleep and brightness values are re-read at the next update. */
void app_power_on_setting(storage_setting_t id);

/* ---- app_flow.c: pet model, request queue and the approval flow (protocol 4.3-4.13) ---- */

typedef enum {
    APP_FLOW_PREVIEW_OFF,
    APP_FLOW_PREVIEW_APPROVAL_LOW,
    APP_FLOW_PREVIEW_APPROVAL_HIGH,
    APP_FLOW_PREVIEW_IDENTIFY,
} app_flow_preview_t;

/** What the flow screens show, for the console `screen` command. */
typedef struct {
    proto_mood_t mood;
    ui_pet_page_t page;
    bool has_request;
    struct {
        reqq_kind_t kind;
        char id[PROTO_ID_SIZE];
        char tool[PROTO_TOOL_SIZE];
        char summary[PROTO_SUMMARY_SIZE];
        proto_risk_t risk;
        uint8_t position;
        uint8_t count;
        uint8_t page;
        uint8_t pages;
        uint8_t seen;
        uint32_t deadline_ms;
        bool answered;
        bool armed;
        bool cut;
        bool decide;
        bool menu;
        card_view_t view;
        /* Questions: 1-based question and count, the options, the focused row, ticks or choice, answered questions. */
        uint8_t question;
        uint8_t questions;
        uint8_t options;
        bool multi;
        uint8_t opt;
        uint8_t mask;
        uint8_t done;
        /* Plans. */
        uint8_t steps;
        uint16_t more;
        card_allow_t allow;
        card_session_t session;
        uint8_t rows;
        card_row_t row[CARD_MENU_ROWS];
        uint8_t cursor;
    } request;
} app_flow_view_t;

esp_err_t app_flow_init(void);

/** App task: status, mood, notify, permission, dismiss, identify, and errors that name a request. */
void app_flow_on_message(const proto_msg_t *msg, uint32_t now);

/** App task: the socket is going away, so the queued requests can no longer be answered here. */
void app_flow_on_link_lost(uint32_t now);

/** App task: a button while a flow screen (pet, approval, identify) is shown; false when it was not used. */
bool app_flow_on_button(const btn_fsm_out_t *out, uint32_t now);

/** App task: the mode of a press on the flow screen shown now. */
btn_mode_t app_flow_btn_mode(btn_id_t id, uint32_t now);

/** App task: a request card or its menu is on screen (power_fsm holds the active stage). */
bool app_flow_card_visible(void);

/** The resting face is on screen: a press there only wakes. */
bool app_flow_clock_visible(void);

/** The room mood is idle or sleeping (power_fsm agent_idle). */
bool app_flow_mood_idle(void);

/* Showcase (protocol 14.7, ux-flows 8). */
bool app_flow_showcase_active(void);
/** False, and nothing changes, while a real request is queued. */
bool app_flow_showcase_enter(uint32_t now);
void app_flow_showcase_on_request(app_request_t *req, uint32_t now);

/** App task: switches the pet screen to its About page. */
void app_flow_show_about(uint32_t now);

/** Console name of a pet-screen page ("pet", "about", "about2"). */
const char *app_flow_page_name(ui_pet_page_t page);

/** App task: an identify or a request owns the screen, so Settings must give way. */
bool app_flow_takes_over(uint32_t now);

/** App task: a toast, also reported as `@@EVT {"evt":"toast",...}`. */
void app_toast(const char *text);
/** A toast with a detail line for ms milliseconds; the console sees its title. */
void app_toast_show(ui_toast_kind_t kind, const char *text, const char *detail, uint32_t ms);
/** app_toast() with the icon and colour of kind. */
void app_toast_kind(ui_toast_kind_t kind, const char *text);

/** App task: a pattern ended; resumes the pending-request or attention loop. */
void app_flow_on_feedback_end(void);

/** App task, before rendering: expiry, holding acks and heartbeats, nags, answer confirmation. */
void app_flow_run_deadlines(uint32_t now);

/** App task, after rendering: arms the timer for the next flow deadline. */
void app_flow_arm_timer(uint32_t now);

/**
 * App task: pushes the content of the flow screen and returns which one to
 * show. offline_banner is NULL while the link is up.
 */
ui_screen_id_t app_flow_render(uint32_t now, const char *offline_banner, app_flow_preview_t preview);

/** Any task: copies the last rendered view. */
void app_flow_get_view(app_flow_view_t *out);
/** App task: a screen outside the flow (the lowbatt card) takes the whole display. */
void app_flow_hide_banner(void);
/** App task: requests in the on-device queue. */
uint8_t app_flow_pending_count(void);

/* ---- app_settings.c: the Settings screen (architecture 4.5) ---- */

bool app_settings_active(void);

/** App task: opens list (the quick menu from home, the top list from start-up screens) with the stored values. */
void app_settings_open(uint32_t now, settings_list_t list);

/** App task: a request covers Settings; when it is answered the top list comes back (ux-flows 1). */
void app_settings_to_top(uint32_t now);

/** App task: closes Settings and the picker (a request or the showcase takes the screen). */
void app_settings_close(void);

/** App task: a button while Settings is on screen. */
void app_settings_on_button(const btn_fsm_out_t *out, uint32_t now);

/** App task: the mode a press gets on the Settings screen (the destructive confirm wants the commit hold). */
btn_mode_t app_settings_btn_mode(btn_id_t id);

/** App task: the view timeouts (ux-flows 6). */
void app_settings_run_deadlines(uint32_t now);

/** The next view timeout, 0 when Settings is closed. */
uint32_t app_settings_deadline(void);

/** App task: APP_REQUEST_SETTINGS. */
void app_settings_on_request(app_request_t *req, uint32_t now);

/** App task: pushes the rows to the screen (the mode row follows the room mode); preview shows them closed too. */
void app_settings_render(bool preview);

/** App task: erases the devotchi namespace and the WiFi driver's configuration, then restarts. */
void app_factory_reset(void);

/** What the Settings screen shows, for the console `screen` command. */
typedef struct {
    bool active;
    uint8_t cursor;
    settings_list_t list;
    settings_item_t item;
    settings_view_t view;
    /** The pet picker is open on page (0 = the first built-in; the last page is "More pets"). */
    bool picker;
    uint8_t page;
} app_settings_view_t;

void app_settings_get_view(app_settings_view_t *out);

/* ---- app_telemetry.c: protocol 4.12 ---- */

/** App task: the server acknowledged the registration. */
void app_telemetry_on_registered(uint32_t now);

/** App task, once a second and on every battery change: periodic and change-driven samples while registered. */
void app_telemetry_tick(uint32_t now);
/** App task, before a battery-protection sleep: one sample with alert battery_critical. */
void app_telemetry_send_final(uint32_t now);

/* ---- requests from the console task to the app task ---- */

#define APP_REQUEST_TEXT_SIZE     1025
#define APP_REQUEST_ARG_SIZE      72
#define APP_REQUEST_MESSAGE_SIZE  64

typedef enum {
    /** text: base URL. */
    APP_REQUEST_SERVER,
    /** text: pairing code. Completes when the server answered the claim. */
    APP_REQUEST_PAIR,
    /** text: SSID, arg: passphrase. Completes with an IP address or after the trial period. */
    APP_REQUEST_WIFI,
    /** text: one protocol message. */
    APP_REQUEST_MSG,
    APP_REQUEST_RESET_PAIRING,
    APP_REQUEST_PAIR_RESTORE,
    /** text: `pairing`, `provisioning`, `approval-low`, `approval-high`, `identify`, `settings` or `off`. */
    APP_REQUEST_PREVIEW,
    /** Renders the screen again from the app's state (after `ui demo off`). */
    APP_REQUEST_RENDER,
    /** Erases the devotchi namespace and the WiFi configuration, then restarts. */
    APP_REQUEST_FACTORY_RESET,
    /** text: `stage`, `dry-run` or `sleep`; arg: the stage, on|off, or the timer wake in seconds. */
    APP_REQUEST_POWER,
    /** text: `use` or `rm`; arg: the pet id. A person's choice, like the picker's Use. */
    APP_REQUEST_PET,
    /** text: `set` (arg: "<epoch_s> <offset_min>") or `show` (arg: day, night or off). */
    APP_REQUEST_CLOCK,
    /** text: `on`, `off` or a page name. */
    APP_REQUEST_SHOWCASE,
    /** text: a settings path (settings_menu_open_path) or `close`. */
    APP_REQUEST_SETTINGS,
} app_request_kind_t;

struct app_request {
    app_request_kind_t kind;
    char text[APP_REQUEST_TEXT_SIZE];
    char arg[APP_REQUEST_ARG_SIZE];
    bool ok;
    char message[APP_REQUEST_MESSAGE_SIZE];
};

typedef enum {
    APP_REQUEST_DONE,
    /** An earlier request that timed out is still being worked on. */
    APP_REQUEST_BUSY,
    APP_REQUEST_TIMEOUT,
} app_request_rc_t;

/**
 * Console task: hands a request to the app task and waits for its completion.
 * On APP_REQUEST_DONE ok and message describe the outcome.
 */
app_request_rc_t app_request(app_request_kind_t kind, const char *text, const char *arg, uint32_t timeout_ms,
                             bool *ok, char message[APP_REQUEST_MESSAGE_SIZE]);

/** App task: the request being worked on, NULL when none. */
app_request_t *app_request_current(void);

/** App task: finishes the current request and wakes the console task. */
void app_request_complete(bool ok, const char *message);

/** App task: APP_REQUEST_POWER. text: `stage`, `dry-run` or `sleep`; arg: its value. */
void app_power_on_request(app_request_t *req);

/** The resting face replaces home (power_fsm show_clock). */
bool app_power_clock_shown(void);

/** The going-to-sleep warning (91) is up; ms_left until the deep sleep. */
bool app_power_sleep_warning(uint32_t *ms_left);

/* ---- app_clock.c: time and the clock settings (protocol 14.9, architecture 7.6) ---- */

/** Console `clock show`: the clock face forced on in its day or night look. */
typedef enum {
    APP_CLOCK_SHOW_AUTO,
    APP_CLOCK_SHOW_DAY,
    APP_CLOCK_SHOW_NIGHT,
} app_clock_show_t;

void app_clock_init(void);
void app_clock_reload_settings(void);
/** register_ack or `time`: sets the system clock and, with has_offset, the offset kept through deep sleep. */
void app_clock_on_time(int64_t epoch_ms, bool has_offset, int16_t offset_min);
/** The time came from a server (after 2026-01-01, with an offset). */
bool app_clock_valid(void);
/** Epoch milliseconds, 0 while the time is not valid. */
int64_t app_clock_epoch_ms(void);
int16_t app_clock_offset_min(void);
/** Local time and date of epoch_ms in the 12/24 h setting; false and "" while there is no offset or epoch_ms is 0. */
bool app_clock_format(int64_t epoch_ms, char time[CLOCK_TIME_SIZE], char date[CLOCK_DATE_SIZE]);
/** 22:00 to 07:00 local with Night dimming on, or the `clock show` override. */
bool app_clock_night(void);
/** Show clock (SETTINGS_CLOCK_*) and Clock brightness. */
uint8_t app_clock_mode(void);
uint8_t app_clock_brightness(void);
void app_clock_set_show(app_clock_show_t show);
app_clock_show_t app_clock_show(void);
void app_clock_on_request(app_request_t *req);

/* ---- pets (app_pets.c): pet_cmd, the `pets` message, the selected pet ---- */

typedef struct {
    char selected[STORAGE_PET_ID_SIZE];
    /** The pet drawn (differs from selected when that one does not load). */
    char shown[STORAGE_PET_ID_SIZE];
    pet_source_t source;
    pet_part_state_t partition;
    size_t slots;
    size_t stored;
    storage_pet_entry_t entries[PET_STORE_SLOTS];
    bool fetching;
} app_pets_view_t;

/** Loads s_pet (or Devo) into the views; after storage_pets_init and ui_init. */
void app_pets_init(void);
/** Sends `pets` after register_ack. */
void app_pets_on_registered(uint32_t now);
void app_pets_on_cmd(const proto_pet_cmd_t *cmd, uint32_t now);
/** The link asks for a pending download; true fills id and marks it started. */
bool app_pets_take_fetch(char id[NET_JOB_ARG_SIZE]);
void app_pets_on_fetch(const net_job_result_t *r, uint32_t now);
/** A pending download could not be submitted (no token): answered with `http`. */
void app_pets_fetch_failed(uint32_t now);
/** A person's choice (picker, console): `unknown` for a pet that is not there. */
pet_result_t app_pets_select(const char *id, uint32_t now);
pet_result_t app_pets_remove(const char *id, uint32_t now);
const char *app_pets_selected(void);
void app_pets_get_view(app_pets_view_t *out);
/** Built-ins and stored pets, the picker's pages before "More pets". */
size_t app_pets_count(void);
/** Decodes pet index (built-ins first) into a buffer that stays valid until the next call; NULL when unreadable. */
const pet_def_t *app_pets_load(size_t index, bool *builtin);

/* ---- pet picker (app_settings.c, 60-62) ---- */

/** The picker is open on top of the settings list. */
bool app_settings_picker(void);
