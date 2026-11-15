#pragma once

#include "ws_reasm.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_TOKEN_SIZE      64
#define NET_ROOM_SIZE       32
#define NET_NAME_SIZE       32
#define NET_CODE_SIZE       8
#define NET_SECRET_SIZE     40
#define NET_DEVICE_ID_SIZE  16

typedef enum {
    NET_WIFI_FAIL_OTHER,
    NET_WIFI_FAIL_WRONG_PASSWORD,
    NET_WIFI_FAIL_NOT_FOUND,
    /** Not a failure: the station left because wifi_connect() or the provisioning manager told it to. */
    NET_WIFI_FAIL_LEFT,
} net_wifi_fail_t;

/** One received text message; owned by the receiver of NET_EV_WS_MSG until ws_msg_release(). */
typedef struct {
    size_t len;
    /** NUL-terminated after len bytes. */
    char data[WS_REASM_CAPACITY + 1];
} net_ws_msg_t;

typedef enum {
    NET_JOB_WS_STOP,
    NET_JOB_PAIR_CLAIM,
    NET_JOB_PAIR_START,
    NET_JOB_PAIR_POLL,
    NET_JOB_HEALTH,
    /** GET /api/pets/<arg>/pack with the token into the pack buffer (net_pet_body). */
    NET_JOB_PET_FETCH,
} net_job_kind_t;

/** Outcome of an HTTP job. Fields the response did not carry are zero or empty. */
typedef struct {
    net_job_kind_t kind;
    /** Echo of the tag given at submission, so a late result can be told from a current one. */
    uint32_t tag;
    /** ESP_OK when an HTTP response was received, whatever its status. */
    esp_err_t err;
    int http_status;
    int proto;
    uint32_t expires_in_s;
    uint32_t poll_interval_ms;
    char token[NET_TOKEN_SIZE];
    char room[NET_ROOM_SIZE];
    char name[NET_NAME_SIZE];
    char code[NET_CODE_SIZE];
    char secret[NET_SECRET_SIZE];
    /** NET_JOB_PET_FETCH: the Content-Length header (-1 absent or chunked), bytes read, and whether the read ended cleanly. */
    int32_t content_length;
    size_t pet_len;
    bool pet_complete;
} net_job_result_t;

/** Body of the last NET_JOB_PET_FETCH; valid until the next pet fetch is submitted. */
const uint8_t *net_pet_body(void);

typedef enum {
    NET_EV_WIFI_GOT_IP,
    /** u.wifi_fail: why the station is not connected. */
    NET_EV_WIFI_DISCONNECTED,
    /** The phone sent credentials and the station is trying them. */
    NET_EV_PROV_CRED_RECV,
    /** u.wifi_fail: why the credentials from the phone did not work. */
    NET_EV_PROV_CRED_FAIL,
    NET_EV_PROV_CRED_SUCCESS,
    NET_EV_PROV_END,
    /** The WebSocket upgrade succeeded. */
    NET_EV_WS_UP,
    /** u.msg: release it with ws_msg_release(). */
    NET_EV_WS_MSG,
    /** u.ws_down: sent once per client when its connection ended or could not be made. */
    NET_EV_WS_DOWN,
    /** The client asked to stop is destroyed; a new one may be started. */
    NET_EV_WS_STOPPED,
    /** u.job: valid until the next HTTP job is submitted. */
    NET_EV_JOB_DONE,
} net_event_id_t;

typedef struct {
    net_event_id_t id;
    union {
        net_wifi_fail_t wifi_fail;
        net_ws_msg_t *msg;
        struct {
            /** Close code sent by the server, 0 when it sent none. */
            uint16_t close_code;
            /** HTTP status of a refused upgrade, 0 when the upgrade was not answered. */
            uint16_t http_status;
        } ws_down;
        const net_job_result_t *job;
    } u;
} net_event_t;

/**
 * Receives every net event. Called from the system event loop, the WebSocket
 * task and the net task, so it only queues the event; false when it could not.
 */
typedef bool (*net_event_cb_t)(const net_event_t *ev);
