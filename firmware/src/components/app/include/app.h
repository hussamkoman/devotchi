#pragma once

#include "btn_fsm.h"
#include "proto.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define APP_EVENT_QUEUE_DEPTH  32
#define APP_DEVICE_ID_SIZE     10
#define APP_MODEL              "devotchi-v1"
#define APP_PROTO_VERSION      2

typedef enum {
    APP_EV_TICK,
    /** arg.u8: btn_id_t, btn_raw_t (from the driver or the console `btn` command). */
    APP_EV_BTN_RAW,
    /** The button tick: posted every BTN_FSM_TICK_MS while a button is down. */
    APP_EV_BTN_TICK,
    /** Console `wake`. */
    APP_EV_USER_ACTIVITY,
    /** arg.u8: fb_seq_id_t, step (FEEDBACK_STEP_END at the end), LED percent, motor percent. */
    APP_EV_FB_STEP,
    /** arg.u32: 1 when the debounced external power flipped. */
    APP_EV_POWER,
    /** arg.u32: storage_setting_t that was written. */
    APP_EV_SETTING_CHANGED,
    APP_EV_WIFI_UP,
    /** arg.u8[0]: net_wifi_fail_t. */
    APP_EV_WIFI_DOWN,
    /** arg.u8: net_event_id_t of the provisioning event, net_wifi_fail_t. */
    APP_EV_PROV,
    APP_EV_WS_UP,
    /** arg.ptr: net_ws_msg_t the handler releases. */
    APP_EV_WS_MSG,
    /** arg.u32: close code in the high half, HTTP status of a refused upgrade in the low half. */
    APP_EV_WS_DOWN,
    APP_EV_WS_STOPPED,
    /** arg.ptr: const net_job_result_t. */
    APP_EV_NET_JOB,
    /** The link state machine's next deadline passed. */
    APP_EV_LINK_TIMER,
    /** A console command waits in the request slot (app_request()). */
    APP_EV_REQUEST,
    /** A deadline of the request flow passed (holding ack, heartbeat, expiry, nag, countdown refresh). */
    APP_EV_FLOW_TIMER,
} app_event_id_t;

/** Passed by value through the app queue; payloads larger than this travel as a pool pointer. */
typedef struct {
    app_event_id_t id;
    /** Milliseconds since boot when the source observed the event; 0 when it does not matter. */
    uint32_t t_ms;
    union {
        uint32_t u32;
        int32_t i32;
        void *ptr;
        uint8_t u8[4];
    } arg;
} app_event_t;

typedef enum {
    APP_STATE_BOOT,
    APP_STATE_PROVISIONING,
    APP_STATE_WIFI_CONNECTING,
    APP_STATE_WIFI_FAILED,
    APP_STATE_PAIRING,
    APP_STATE_SERVER_CONNECTING,
    APP_STATE_ONLINE,
    APP_STATE_OFFLINE,
} app_state_t;

typedef enum {
    APP_WIFI_OFF,
    APP_WIFI_INIT_FAILED,
    /** Storage is degraded and holds no credentials; the console can supply them. */
    APP_WIFI_NO_CREDENTIALS,
    APP_WIFI_PROVISIONING,
    APP_WIFI_CONNECTING,
    /** The last attempt failed; the next one is scheduled. */
    APP_WIFI_FAILED,
    APP_WIFI_CONNECTED,
} app_wifi_state_t;

typedef enum {
    APP_WS_DOWN,
    APP_WS_CONNECTING,
    APP_WS_CONNECTED,
    APP_WS_REGISTERED,
} app_ws_state_t;

/** Lifecycle of the one WebSocket client; a client is created only in APP_WS_PHASE_IDLE. */
typedef enum {
    APP_WS_PHASE_IDLE,
    APP_WS_PHASE_RUNNING,
    APP_WS_PHASE_STOPPING,
} app_ws_phase_t;

#define APP_LINK_ERROR_SIZE  48
#define APP_SSID_SIZE        33
#define APP_SERVER_SIZE      128

typedef struct {
    app_wifi_state_t wifi;
    app_ws_state_t ws;
    app_ws_phase_t ws_phase;
    /** The SSID the device is configured for, connected or not. */
    char ssid[APP_SSID_SIZE];
    /** The server base URL in use; it differs from storage only when storage is degraded. */
    char server[APP_SERVER_SIZE];
    bool paired;
    char last_error[APP_LINK_ERROR_SIZE];
    /** app_now_ms() of the next reconnect attempt, 0 when none is scheduled. */
    uint32_t retry_at_ms;
    /** Consecutive upgrades refused with HTTP 401 by a protocol 2 server. */
    uint8_t rejected_401;
    uint32_t rx_invalid;
    proto_mode_t mode;
    /** PROTO_CAP_* of the server's register_ack; 0 before it and for a phase-1 server. */
    uint32_t server_caps;
    char name[PROTO_NAME_SIZE];
    /** A `preview` screen with sample data is up. */
    bool preview;
} app_link_info_t;

typedef struct {
    app_state_t state;
    app_link_info_t link;
    uint32_t events_dropped;
    uint32_t last_user_ms;
    /** Last status, mood or notify from an agent; postpones deep sleep only. */
    uint32_t last_agent_ms;
    bool has_last_btn;
    btn_fsm_out_t last_btn;
    uint32_t last_btn_ms;
} app_snapshot_t;

/**
 * Creates the event queue, initialises the drivers that report to the app
 * (buttons, feedback, power, net) and registers the application's console commands.
 */
esp_err_t app_init(void);

/** Starts the app task, the pwr task and the 1 Hz tick. */
esp_err_t app_start(void);

uint32_t app_now_ms(void);

/** Posts an event to the app task; returns false if the queue stayed full for 10 ms. */
bool app_post(const app_event_t *ev);

/** Posts an event from an ISR; returns false if the queue is full. */
bool app_post_from_isr(const app_event_t *ev);

/** Copies the application state; safe from any task. */
void app_get_snapshot(app_snapshot_t *out);

const char *app_state_name(app_state_t state);
const char *app_wifi_state_name(app_wifi_state_t state);
const char *app_ws_state_name(app_ws_state_t state);
const char *app_ws_phase_name(app_ws_phase_t phase);

/** Reset reason of this boot as a protocol string (`poweron`, `sw`, `panic`, ...). */
const char *app_reset_reason(void);

/** Writes "dv-" followed by the last three bytes of the base MAC in lowercase hex. */
void app_get_device_id(char id[APP_DEVICE_ID_SIZE]);

uint32_t app_uptime_s(void);
