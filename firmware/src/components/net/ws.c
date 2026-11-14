#include "ws.h"

#include "net_priv.h"
#include "net_worker.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ws";

#define SEND_TIMEOUT_MS      1000
#define NETWORK_TIMEOUT_MS   10000
#define PING_INTERVAL_S      20
#define PONG_TIMEOUT_S       60
#define AUTH_HEADER_SIZE     (sizeof("Authorization: Bearer \r\n") + NET_TOKEN_SIZE)

static net_event_cb_t s_cb;
static const char *s_task_name;
static uint32_t s_task_stack;
static uint32_t s_task_prio;

/* Created by the app task in ws_start(), destroyed by the net task in ws_destroy(); the app's
 * lifecycle (idle -> running -> stopping -> idle) keeps the two from overlapping. */
static esp_websocket_client_handle_t s_client;

static net_ws_msg_t *s_pool;
static atomic_bool s_pool_used[WS_MSG_POOL_SIZE];
static atomic_uint s_rx_dropped;

/* WebSocket task only, apart from ws_start() preparing them before the task exists. */
static ws_reasm_t s_reasm;
static uint16_t s_close_code;
static uint16_t s_http_status;
static bool s_down_posted;

static net_ws_msg_t *pool_take(void)
{
    for (size_t i = 0; i < WS_MSG_POOL_SIZE; i++) {
        bool expected = false;
        if (atomic_compare_exchange_strong(&s_pool_used[i], &expected, true)) {
            return &s_pool[i];
        }
    }
    return NULL;
}

void ws_msg_release(net_ws_msg_t *msg)
{
    if (msg == NULL || msg < s_pool || msg >= s_pool + WS_MSG_POOL_SIZE) {
        return;
    }
    atomic_store(&s_pool_used[msg - s_pool], false);
}

uint32_t ws_rx_dropped(void)
{
    return atomic_load(&s_rx_dropped);
}

static void deliver_message(void)
{
    net_ws_msg_t *msg = pool_take();
    if (msg == NULL) {
        atomic_fetch_add(&s_rx_dropped, 1);
        return;
    }
    memcpy(msg->data, s_reasm.buf, s_reasm.len);
    msg->data[s_reasm.len] = '\0';
    msg->len = s_reasm.len;
    const net_event_t ev = {.id = NET_EV_WS_MSG, .u.msg = msg};
    if (!s_cb(&ev)) {
        ws_msg_release(msg);
        atomic_fetch_add(&s_rx_dropped, 1);
    }
}

static void post_down_once(void)
{
    if (s_down_posted) {
        return;
    }
    s_down_posted = true;
    const net_event_t ev = {
        .id = NET_EV_WS_DOWN,
        .u.ws_down = {.close_code = s_close_code, .http_status = s_http_status},
    };
    s_cb(&ev);
}

/* Runs in the WebSocket task: copies and posts only. */
static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    (void)base;
    const esp_websocket_event_data_t *data = event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        ws_reasm_reset(&s_reasm);
        s_close_code = 0;
        s_http_status = 0;
        const net_event_t ev = {.id = NET_EV_WS_UP};
        s_cb(&ev);
        break;
    }
    case WEBSOCKET_EVENT_DATA:
        switch (ws_reasm_feed(&s_reasm, data->op_code, data->fin, data->payload_len, data->payload_offset,
                              data->data_ptr, data->data_len)) {
        case WS_REASM_MESSAGE:
            deliver_message();
            break;
        case WS_REASM_DROPPED:
            atomic_fetch_add(&s_rx_dropped, 1);
            break;
        case WS_REASM_CLOSE:
            s_close_code = s_reasm.close_code;
            break;
        case WS_REASM_NONE:
            break;
        }
        break;
    case WEBSOCKET_EVENT_ERROR: {
        const int status = data->error_handle.esp_ws_handshake_status_code;
        if (status > 0 && status <= UINT16_MAX) {
            s_http_status = (uint16_t)status;
        }
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_FINISH:
        post_down_once();
        break;
    default:
        break;
    }
}

esp_err_t ws_init(net_event_cb_t cb, const char *task_name, uint32_t task_stack, uint32_t task_prio)
{
    s_cb = cb;
    s_task_name = task_name;
    s_task_stack = task_stack;
    s_task_prio = task_prio;
    s_pool = heap_caps_calloc(WS_MSG_POOL_SIZE, sizeof(net_ws_msg_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_pool != NULL, ESP_ERR_NO_MEM, TAG, "message pool");
    return ESP_OK;
}

esp_err_t ws_start(const char *uri, const char *token)
{
    ESP_RETURN_ON_FALSE(s_client == NULL, ESP_ERR_INVALID_STATE, TAG, "a client exists");
    ESP_RETURN_ON_FALSE(uri != NULL && token != NULL && token[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "arguments");

    /*
     * transport_ws prints the whole upgrade request, Authorization header
     * included, when writing it fails (transport_ws.c, "Error write Upgrade
     * header"). The client reports the same failures without the request.
     * Set before every connect because the console command `log` resets
     * per-tag levels.
     */
    esp_log_level_set("transport_ws", ESP_LOG_NONE);

    /* The client copies uri and headers during init. */
    static char headers[AUTH_HEADER_SIZE];
    const int n = snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
    ESP_RETURN_ON_FALSE(n > 0 && (size_t)n < sizeof(headers), ESP_ERR_INVALID_SIZE, TAG, "token too long");

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .headers = headers,
        .buffer_size = CONFIG_DEVOTCHI_WS_RX_BUFFER,
        .task_name = s_task_name,
        .task_stack = (int)s_task_stack,
        .task_prio = (int)s_task_prio,
        .disable_auto_reconnect = true,
        .network_timeout_ms = NETWORK_TIMEOUT_MS,
        .ping_interval_sec = PING_INTERVAL_S,
        .pingpong_timeout_sec = PONG_TIMEOUT_S,
    };
    tls_cfg_apply_ws(&cfg);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&cfg);
    memset(headers, 0, sizeof(headers));
    ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "client init");

    s_down_posted = false;
    s_close_code = 0;
    s_http_status = 0;
    esp_err_t err = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL);
    if (err == ESP_OK) {
        err = esp_websocket_client_start(client);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client);
        return err;
    }
    s_client = client;
    return ESP_OK;
}

bool ws_send(const char *buf, size_t len)
{
    if (s_client == NULL || buf == NULL || len == 0 || len > INT32_MAX) {
        return false;
    }
    return esp_websocket_client_send_text(s_client, buf, (int)len, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) == (int)len;
}

bool ws_request_stop(void)
{
    const net_job_t job = {.kind = NET_JOB_WS_STOP};
    return net_worker_submit(&job);
}

void ws_destroy(void)
{
    if (s_client == NULL) {
        return;
    }
    /* Waits without a time limit for the client task, which may sit in connect or a TLS handshake. */
    esp_websocket_client_destroy(s_client);
    s_client = NULL;
}
