#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define APP_EVENT_QUEUE_DEPTH  32
#define APP_DEVICE_ID_SIZE     10
#define APP_MODEL              "devotchi-v1"
#define APP_PROTO_VERSION      2

typedef enum {
    APP_EV_TICK,
} app_event_id_t;

/** Passed by value through the app queue; payloads larger than this travel as a pool pointer. */
typedef struct {
    app_event_id_t id;
    union {
        uint32_t u32;
        int32_t i32;
        void *ptr;
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

typedef struct {
    app_state_t state;
    uint32_t events_dropped;
} app_snapshot_t;

/** Creates the event queue and registers the application's console commands. */
esp_err_t app_init(void);

/** Starts the app task and the 1 Hz tick. */
esp_err_t app_start(void);

/** Posts an event to the app task; returns false if the queue stayed full for 10 ms. */
bool app_post(const app_event_t *ev);

/** Posts an event from an ISR; returns false if the queue is full. */
bool app_post_from_isr(const app_event_t *ev);

/** Copies the application state; safe from any task. */
void app_get_snapshot(app_snapshot_t *out);

const char *app_state_name(app_state_t state);

/** Reset reason of this boot as a protocol string (`poweron`, `sw`, `panic`, ...). */
const char *app_reset_reason(void);

/** Writes "dv-" followed by the last three bytes of the base MAC in lowercase hex. */
void app_get_device_id(char id[APP_DEVICE_ID_SIZE]);

uint32_t app_uptime_s(void);
