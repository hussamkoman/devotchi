#pragma once

#include "app.h"
#include "storage.h"
#include "ui.h"

#include "esp_err.h"

#include <stdbool.h>

/* Diag screen lines: 0 and 1 are fixed at start, the link state machine owns 2 and 3. */
#define APP_DIAG_LINE_LINK    2
#define APP_DIAG_LINE_SERVER  3
#define APP_DIAG_LINE_BATT    4
#define APP_DIAG_LINE_BTN     5

esp_err_t app_cmds_register(void);

/**
 * Tells the app task that a setting was written and waits until it applied it;
 * false when the queue is full or the app task did not get to it in time.
 * One caller at a time (the console task), never the app task itself.
 */
bool app_setting_changed(storage_setting_t id);

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

/** App task: a click or long press that was not swallowed. */
void app_link_on_button(const btn_fsm_out_t *out);

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
    /** text: `pairing`, `provisioning` or `off`. */
    APP_REQUEST_PREVIEW,
} app_request_kind_t;

typedef struct {
    app_request_kind_t kind;
    char text[APP_REQUEST_TEXT_SIZE];
    char arg[APP_REQUEST_ARG_SIZE];
    bool ok;
    char message[APP_REQUEST_MESSAGE_SIZE];
} app_request_t;

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
