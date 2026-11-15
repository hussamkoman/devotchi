#include "app_priv.h"

#include "app.h"
#include "app_tasks.h"
#include "backoff.h"
#include "console.h"
#include "net_worker.h"
#include "pair_code.h"
#include "power.h"
#include "prov.h"
#include "proto.h"
#include "storage.h"
#include "text.h"
#include "ui.h"
#include "url.h"
#include "wifi.h"
#include "ws.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "link";

#define WIFI_BACKOFF_MIN_MS     1000
#define WIFI_BACKOFF_MAX_MS     30000
#define WS_BACKOFF_MIN_MS       1000
#define WS_BACKOFF_MAX_MS       60000
#define PAIR_BACKOFF_MIN_MS     2000
#define PAIR_BACKOFF_MAX_MS     60000
/* Protocol 3.4: this long without any frame from the server means the link is dead. */
#define LINK_SILENCE_MS         70000
#define SEND_FAILURES_FOR_LOSS  2
#define STOP_WARN_MS            15000
#define STOP_RESUBMIT_MS        200
/* Protocol 9.4: a token is given up only after this many 401s spread over this time. */
#define REVOKE_MIN_COUNT        3
#define REVOKE_MIN_SPAN_MS      30000
#define INCOMPATIBLE_RETRY_MS   60000
/* Protocol 3.6: close code 4002. */
#define UPDATE_RETRY_MS         (5 * 60 * 1000)
#define WIFI_TRIAL_MS           20000
#define WIFI_HINT_FAILURES      5
#define POLL_MIN_MS             1000
#define POLL_MAX_MS             10000
#define POLL_DEFAULT_MS         2000
#define RATE_LIMIT_WAIT_MS      10000
#define CODE_DEFAULT_LIFE_S     300
#define CLOSE_REVOKED           4003
#define CLOSE_UNSUPPORTED       4002
#define HTTP_UNAUTHORIZED       401
#define TX_SIZE                 512
#define LINE_SIZE               64

typedef enum {
    PAIR_IDLE,
    /** Waiting for the server to issue a code. */
    PAIR_STARTING,
    /** A code is on screen and the server is polled for its confirmation. */
    PAIR_SHOWING,
} pair_phase_t;

typedef enum {
    PREVIEW_OFF,
    PREVIEW_PAIRING,
    PREVIEW_PROVISIONING,
    PREVIEW_APPROVAL_LOW,
    PREVIEW_APPROVAL_HIGH,
    PREVIEW_IDENTIFY,
    PREVIEW_SETTINGS,
} preview_t;

/* Everything below belongs to the app task. */

static app_state_t s_state = APP_STATE_BOOT;
static app_link_info_t s_info;
static bool s_splash_done;
/* This boot is a wake from deep sleep: no splash, and the pet shows while reconnecting. */
static bool s_deep_wake;
static bool s_online_seen;
/* Deep sleep is about to start: no further connection attempts. */
static bool s_suspended;
static bool s_screen_valid;
static ui_screen_id_t s_screen;
/* The pet, approval or identify screen is up, so buttons belong to the request flow. */
static bool s_flow_shown;
static preview_t s_preview;
static esp_timer_handle_t s_timer;

static char s_device_id[APP_DEVICE_ID_SIZE];
static char s_token[STORAGE_TOKEN_SIZE];
static char s_url_text[STORAGE_URL_SIZE];
static url_t s_url;
static bool s_url_valid;

/* WiFi */
static esp_err_t s_wifi_init_err;
static bool s_wifi_up;
static char s_ssid[STORAGE_SSID_SIZE];
static char s_psk[STORAGE_PSK_SIZE];
static backoff_t s_wifi_backoff;
static uint32_t s_wifi_retry_at;
static uint32_t s_wifi_failures;
static net_wifi_fail_t s_wifi_fail;
static struct {
    bool active;
    uint32_t deadline;
    char ssid[STORAGE_SSID_SIZE];
    char psk[STORAGE_PSK_SIZE];
} s_trial;

/* Provisioning */
static prov_info_t s_prov;
static char s_prov_status[LINE_SIZE];

/* WebSocket */
static bool s_ws_connected;
static bool s_registered_this_boot;
static bool s_want_connect;
static bool s_stop_submitted;
static bool s_stop_warned;
static uint32_t s_stop_since;
static uint32_t s_stop_retry_at;
static backoff_t s_ws_backoff;
static uint32_t s_last_rx_ms;
static uint8_t s_send_failures;
static uint32_t s_first_401_ms;
static bool s_unsupported_proto;
static proto_msg_t s_msg;
static char s_tx[TX_SIZE];

/* HTTP jobs: one at a time on the net task; the others wait as intents. */
static bool s_http_busy;
static uint32_t s_generation;
static bool s_want_claim;
static bool s_claim_in_flight;
static char s_claim_code[PAIR_CODE_SIZE];
static bool s_want_health;
static bool s_health_in_flight;
static bool s_want_pair_start;
static bool s_want_pair_poll;

/* Pairing flow A */
static struct {
    pair_phase_t phase;
    char code[PAIR_CODE_SIZE];
    char secret[NET_SECRET_SIZE];
    uint32_t total_s;
    uint32_t expires_at;
    uint32_t poll_ms;
    uint32_t next_poll_at;
    uint32_t retry_at;
    backoff_t backoff;
    char banner[LINE_SIZE];
    char error[LINE_SIZE];
} s_pair;

/* ---- small helpers ---- */

static uint32_t rand_fn(void *ctx)
{
    (void)ctx;
    return esp_random();
}

/* Deadlines are app_now_ms() values; 0 means "none", so a real deadline is never 0. */
static uint32_t after(uint32_t now, uint32_t ms)
{
    const uint32_t t = now + ms;
    return t != 0 ? t : 1;
}

static bool due(uint32_t at, uint32_t now)
{
    return at != 0 && (int32_t)(now - at) >= 0;
}

static uint32_t seconds_until(uint32_t at, uint32_t now)
{
    if (at == 0 || (int32_t)(at - now) <= 0) {
        return 0;
    }
    return (at - now + 999) / 1000;
}

static void set_error(const char *text)
{
    strlcpy(s_info.last_error, text, sizeof(s_info.last_error));
}

static void set_state(app_state_t state)
{
    const app_state_t prev = s_state;
    s_state = state;
    app_set_state(state);
    if (state == prev) {
        return;
    }
    /*
     * Only these transitions are user activity (architecture 4.8). Retries
     * and reconnect cycles are not, so an unreachable server cannot keep the
     * screen on; leaving ONLINE happens once per outage.
     */
    bool activity = state == APP_STATE_PAIRING || state == APP_STATE_PROVISIONING || prev == APP_STATE_ONLINE;
    if (state == APP_STATE_ONLINE && !s_online_seen) {
        s_online_seen = true;
        activity = true;
    }
    if (activity) {
        app_user_activity(app_now_ms());
    }
}

static bool is_server_state(void)
{
    return s_state == APP_STATE_SERVER_CONNECTING || s_state == APP_STATE_ONLINE || s_state == APP_STATE_OFFLINE;
}

static void load_server_url(void)
{
    storage_get_server_url(s_url_text, NULL);
    strlcpy(s_info.server, s_url_text, sizeof(s_info.server));
    s_url_valid = url_parse(s_url_text, &s_url);
    if (!s_url_valid) {
        ESP_LOGE(TAG, "stored server url is not valid");
    }
}

/* ---- events to the app queue ---- */

static bool on_net_event(const net_event_t *ev)
{
    app_event_t out = {.t_ms = app_now_ms()};
    switch (ev->id) {
    case NET_EV_WIFI_GOT_IP:
        out.id = APP_EV_WIFI_UP;
        break;
    case NET_EV_WIFI_DISCONNECTED:
        out.id = APP_EV_WIFI_DOWN;
        out.arg.u8[0] = (uint8_t)ev->u.wifi_fail;
        break;
    case NET_EV_PROV_CRED_RECV:
    case NET_EV_PROV_CRED_FAIL:
    case NET_EV_PROV_CRED_SUCCESS:
    case NET_EV_PROV_END:
        out.id = APP_EV_PROV;
        out.arg.u8[0] = (uint8_t)ev->id;
        out.arg.u8[1] = (uint8_t)ev->u.wifi_fail;
        break;
    case NET_EV_WS_UP:
        out.id = APP_EV_WS_UP;
        break;
    case NET_EV_WS_MSG:
        out.id = APP_EV_WS_MSG;
        out.arg.ptr = ev->u.msg;
        break;
    case NET_EV_WS_DOWN:
        out.id = APP_EV_WS_DOWN;
        out.arg.u32 = ((uint32_t)ev->u.ws_down.close_code << 16) | ev->u.ws_down.http_status;
        break;
    case NET_EV_WS_STOPPED:
        out.id = APP_EV_WS_STOPPED;
        break;
    case NET_EV_JOB_DONE:
        out.id = APP_EV_NET_JOB;
        /* The slot is only read; app_event_t has one pointer member for both directions. */
        out.arg.ptr = (void *)ev->u.job;
        break;
    }
    return app_post(&out);
}

static void on_timer(void *arg)
{
    (void)arg;
    const app_event_t ev = {.id = APP_EV_LINK_TIMER, .t_ms = app_now_ms()};
    app_post(&ev);
}

/* ---- sending ---- */

static void request_stop(uint32_t now);
static void link_lost(uint32_t now, const char *why, uint32_t min_delay_ms);

bool app_link_send(uint32_t now, const char *type, const char *frame, int len, const char *extra)
{
    bool sent = false;
    if (len > 0 && s_info.ws_phase == APP_WS_PHASE_RUNNING && s_ws_connected) {
        sent = ws_send(frame, (size_t)len);
        if (sent) {
            s_send_failures = 0;
        } else {
            s_send_failures++;
        }
    }
    char evt[192];
    snprintf(evt, sizeof(evt), "{\"evt\":\"tx\",\"type\":\"%s\",\"sent\":%s%s}", type, sent ? "true" : "false",
             extra != NULL ? extra : "");
    console_evt(evt);
    /* One failed send is retried by the next heartbeat; two in a row mean the link is gone. */
    if (s_send_failures >= SEND_FAILURES_FOR_LOSS) {
        s_send_failures = 0;
        link_lost(now, "Send failed", 0);
    }
    return sent;
}

static bool link_send(uint32_t now, const char *type, int len, const char *extra)
{
    return app_link_send(now, type, s_tx, len, extra);
}

bool app_link_flow_shown(void)
{
    return s_flow_shown;
}

uint32_t app_link_server_caps(void)
{
    return s_info.server_caps;
}

proto_mode_t app_link_mode(void)
{
    return s_info.mode;
}

const char *app_link_name(void)
{
    return s_info.name;
}

/* ---- WebSocket lifecycle ---- */

static void request_stop(uint32_t now)
{
    if (s_info.ws_phase != APP_WS_PHASE_RUNNING) {
        return;
    }
    s_info.ws_phase = APP_WS_PHASE_STOPPING;
    s_info.ws = APP_WS_DOWN;
    s_ws_connected = false;
    s_stop_since = now;
    s_stop_warned = false;
    s_stop_submitted = ws_request_stop();
    s_stop_retry_at = s_stop_submitted ? 0 : after(now, STOP_RESUBMIT_MS);
    /* The server resolves held requests as holder_lost once the socket is gone (protocol 8.3). */
    app_flow_on_link_lost(now);
}

static void try_connect(uint32_t now)
{
    if (!s_wifi_up || s_token[0] == '\0' || s_suspended) {
        return;
    }
    if (s_info.ws_phase != APP_WS_PHASE_IDLE) {
        /* Picked up when APP_EV_WS_STOPPED arrives: a second client is never created. */
        s_want_connect = true;
        request_stop(now);
        return;
    }
    s_want_connect = false;
    s_info.retry_at_ms = 0;

    char uri[URL_FULL_SIZE];
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (s_url_valid && url_ws(&s_url, uri, sizeof(uri)) > 0) {
        err = ws_start(uri, s_token);
    }
    if (err != ESP_OK) {
        set_error("Cannot start connection");
        s_info.retry_at_ms = after(now, backoff_next(&s_ws_backoff));
        return;
    }
    s_info.ws_phase = APP_WS_PHASE_RUNNING;
    s_info.ws = APP_WS_CONNECTING;
    s_ws_connected = false;
    s_send_failures = 0;
    s_unsupported_proto = false;
    s_last_rx_ms = now;
}

/* Stops the client and schedules the next attempt. min_delay_ms is a floor under the backoff. */
static void link_lost(uint32_t now, const char *why, uint32_t min_delay_ms)
{
    request_stop(now);
    s_info.ws = APP_WS_DOWN;
    if (why != NULL) {
        set_error(why);
    }
    if (!is_server_state()) {
        return;
    }
    set_state(s_registered_this_boot ? APP_STATE_OFFLINE : APP_STATE_SERVER_CONNECTING);
    uint32_t delay = backoff_next(&s_ws_backoff);
    if (delay < min_delay_ms) {
        delay = min_delay_ms;
    }
    s_info.retry_at_ms = after(now, delay);
    ESP_LOGW(TAG, "link down (%s), retry in %lu ms", s_info.last_error, (unsigned long)delay);
}

/* ---- pairing ---- */

/* Results of HTTP jobs submitted before this point no longer apply. */
static void new_generation(void)
{
    s_generation++;
    s_want_health = false;
    s_health_in_flight = false;
    s_want_pair_start = false;
    s_want_pair_poll = false;
    s_pair.phase = PAIR_IDLE;
    s_pair.code[0] = '\0';
    memset(s_pair.secret, 0, sizeof(s_pair.secret));
    s_pair.next_poll_at = 0;
    s_pair.retry_at = 0;
    s_pair.expires_at = 0;
}

static void pair_request_code(void)
{
    s_pair.phase = PAIR_STARTING;
    s_pair.code[0] = '\0';
    s_pair.next_poll_at = 0;
    s_pair.retry_at = 0;
    s_pair.expires_at = 0;
    s_want_pair_poll = false;
    s_want_pair_start = true;
}

static void enter_pairing(const char *banner)
{
    new_generation();
    s_info.retry_at_ms = 0;
    s_info.rejected_401 = 0;
    backoff_reset(&s_pair.backoff);
    strlcpy(s_pair.banner, banner != NULL ? banner : "", sizeof(s_pair.banner));
    s_pair.error[0] = '\0';
    set_state(APP_STATE_PAIRING);
    if (s_wifi_up) {
        pair_request_code();
    }
}

static void store_pairing(const net_job_result_t *r)
{
    const esp_err_t err = storage_set_pairing(r->token, r->room);
    if (err != ESP_OK) {
        /* Degraded storage: the pairing lasts until the next reset. */
        ESP_LOGW(TAG, "pairing not persisted: %s", esp_err_to_name(err));
    }
    strlcpy(s_token, r->token, sizeof(s_token));
    s_info.paired = true;
    if (r->name[0] != '\0') {
        storage_set_dev_name(r->name);
        strlcpy(s_info.name, r->name, sizeof(s_info.name));
    }
    ESP_LOGI(TAG, "paired");
}

static void forget_pairing(void)
{
    const esp_err_t err = storage_clear_pairing();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pairing not erased: %s", esp_err_to_name(err));
    }
    memset(s_token, 0, sizeof(s_token));
    s_info.paired = false;
}

/* Drops what is in progress and starts again from the stored token and server URL. */
static void reconnect_now(uint32_t now)
{
    new_generation();
    load_server_url();
    backoff_reset(&s_ws_backoff);
    s_info.retry_at_ms = 0;
    s_info.rejected_401 = 0;
    s_info.last_error[0] = '\0';
    request_stop(now);
    if (!s_wifi_up) {
        return;
    }
    if (s_token[0] == '\0') {
        enter_pairing(NULL);
        return;
    }
    set_state(APP_STATE_SERVER_CONNECTING);
    try_connect(now);
}

static void on_wifi_ready(uint32_t now)
{
    if (s_token[0] != '\0') {
        set_state(s_registered_this_boot ? APP_STATE_OFFLINE : APP_STATE_SERVER_CONNECTING);
        try_connect(now);
    } else {
        enter_pairing(NULL);
    }
}

static const char *http_error_text(const net_job_result_t *r, char buf[LINE_SIZE])
{
    if (r->err != ESP_OK) {
        return "Server unreachable";
    }
    switch (r->http_status) {
    case 404:
        return "Unknown code";
    case 410:
        return "Code expired";
    case 429:
        return "Too many attempts, wait";
    default:
        snprintf(buf, LINE_SIZE, "Server error %d", r->http_status);
        return buf;
    }
}

static void on_pair_start_result(const net_job_result_t *r, uint32_t now)
{
    if (s_pair.phase != PAIR_STARTING) {
        return;
    }
    if (r->err == ESP_OK && r->http_status == 201 && pair_code_is_valid(r->code) && r->secret[0] != '\0') {
        backoff_reset(&s_pair.backoff);
        s_pair.phase = PAIR_SHOWING;
        pair_code_normalise(r->code, s_pair.code);
        strlcpy(s_pair.secret, r->secret, sizeof(s_pair.secret));
        s_pair.total_s = r->expires_in_s != 0 ? r->expires_in_s : CODE_DEFAULT_LIFE_S;
        s_pair.expires_at = after(now, s_pair.total_s * 1000);
        s_pair.poll_ms = r->poll_interval_ms != 0 ? r->poll_interval_ms : POLL_DEFAULT_MS;
        if (s_pair.poll_ms < POLL_MIN_MS) {
            s_pair.poll_ms = POLL_MIN_MS;
        } else if (s_pair.poll_ms > POLL_MAX_MS) {
            s_pair.poll_ms = POLL_MAX_MS;
        }
        s_pair.next_poll_at = after(now, s_pair.poll_ms);
        s_pair.error[0] = '\0';
        return;
    }
    char buf[LINE_SIZE];
    const char *text = r->err == ESP_OK && r->http_status == 201 ? "Bad answer from server" : http_error_text(r, buf);
    strlcpy(s_pair.error, text, sizeof(s_pair.error));
    s_pair.retry_at = after(now, backoff_next(&s_pair.backoff));
}

static void on_pair_poll_result(const net_job_result_t *r, uint32_t now)
{
    if (s_pair.phase != PAIR_SHOWING) {
        return;
    }
    if (r->err == ESP_OK && r->http_status == 200 && r->token[0] != '\0') {
        store_pairing(r);
        new_generation();
        s_pair.banner[0] = '\0';
        backoff_reset(&s_ws_backoff);
        set_state(APP_STATE_SERVER_CONNECTING);
        try_connect(now);
        return;
    }
    if (r->err == ESP_OK && r->http_status == 202) {
        backoff_reset(&s_pair.backoff);
        s_pair.error[0] = '\0';
        s_pair.next_poll_at = after(now, s_pair.poll_ms);
        return;
    }
    if (r->err == ESP_OK && (r->http_status == 410 || r->http_status == 404)) {
        pair_request_code();
        return;
    }
    char buf[LINE_SIZE];
    strlcpy(s_pair.error, http_error_text(r, buf), sizeof(s_pair.error));
    const uint32_t wait = r->err == ESP_OK && r->http_status == 429 ? RATE_LIMIT_WAIT_MS : backoff_next(&s_pair.backoff);
    s_pair.next_poll_at = after(now, wait);
}

static void on_claim_result(const net_job_result_t *r, uint32_t now)
{
    s_claim_in_flight = false;
    if (r->err == ESP_OK && r->http_status == 201 && r->token[0] != '\0') {
        store_pairing(r);
        s_pair.banner[0] = '\0';
        reconnect_now(now);
        app_request_complete(true, "");
        return;
    }
    char buf[LINE_SIZE];
    app_request_complete(false, r->err == ESP_OK && r->http_status == 201 ? "Bad answer from server"
                                                                          : http_error_text(r, buf));
}

/* Protocol 9.4: a 401 counts against the token only when the server proves to speak protocol 2. */
static void on_health_result(const net_job_result_t *r, uint32_t now)
{
    s_health_in_flight = false;
    if (!is_server_state() || s_token[0] == '\0') {
        return;
    }
    if (r->err != ESP_OK || r->http_status != 200 || r->proto != PROTO_VERSION) {
        link_lost(now, "Incompatible server", INCOMPATIBLE_RETRY_MS);
        return;
    }
    if (s_info.rejected_401 == 0) {
        s_first_401_ms = now;
    }
    if (s_info.rejected_401 < UINT8_MAX) {
        s_info.rejected_401++;
    }
    if (s_info.rejected_401 >= REVOKE_MIN_COUNT && now - s_first_401_ms >= REVOKE_MIN_SPAN_MS) {
        ESP_LOGW(TAG, "token rejected %u times, pairing erased", s_info.rejected_401);
        forget_pairing();
        set_error("Server rejected token");
        enter_pairing("Server rejected token");
        return;
    }
    link_lost(now, "Server rejected token, retrying", 0);
}

static void on_job_result(const net_job_result_t *r, uint32_t now)
{
    s_http_busy = false;
    if (r->kind == NET_JOB_PAIR_CLAIM) {
        on_claim_result(r, now);
        return;
    }
    /* A download answers its pet_cmd even when the link restarted meanwhile. */
    if (r->kind == NET_JOB_PET_FETCH) {
        app_pets_on_fetch(r, now);
        return;
    }
    if (r->tag != s_generation) {
        return;
    }
    switch (r->kind) {
    case NET_JOB_PAIR_START:
        on_pair_start_result(r, now);
        break;
    case NET_JOB_PAIR_POLL:
        on_pair_poll_result(r, now);
        break;
    case NET_JOB_HEALTH:
        on_health_result(r, now);
        break;
    case NET_JOB_PAIR_CLAIM:
    case NET_JOB_PET_FETCH:
    case NET_JOB_WS_STOP:
        break;
    }
}

static void submit_next_job(void)
{
    if (s_http_busy || !s_wifi_up || s_suspended) {
        return;
    }
    /* Static: a job carries the server URL and does not belong on the stack. */
    static net_job_t job;
    memset(&job, 0, sizeof(job));
    job.tag = s_generation;
    strlcpy(job.base_url, s_url_text, sizeof(job.base_url));
    bool *intent;
    if (s_want_claim) {
        job.kind = NET_JOB_PAIR_CLAIM;
        strlcpy(job.arg, s_claim_code, sizeof(job.arg));
        intent = &s_want_claim;
    } else if (s_want_health) {
        job.kind = NET_JOB_HEALTH;
        intent = &s_want_health;
    } else if (s_want_pair_start) {
        job.kind = NET_JOB_PAIR_START;
        intent = &s_want_pair_start;
    } else if (s_want_pair_poll) {
        job.kind = NET_JOB_PAIR_POLL;
        strlcpy(job.arg, s_pair.secret, sizeof(job.arg));
        intent = &s_want_pair_poll;
    } else if (s_token[0] != '\0' && app_pets_take_fetch(job.arg)) {
        job.kind = NET_JOB_PET_FETCH;
        strlcpy(job.token, s_token, sizeof(job.token));
        /* app_pets_take_fetch already took the intent. */
        intent = NULL;
    } else {
        return;
    }
    if (!net_worker_submit(&job)) {
        if (job.kind == NET_JOB_PET_FETCH) {
            app_pets_fetch_failed(app_now_ms());
        }
    } else {
        s_http_busy = true;
        if (intent != NULL) {
            *intent = false;
        }
        if (job.kind == NET_JOB_PAIR_CLAIM) {
            s_claim_in_flight = true;
        } else if (job.kind == NET_JOB_HEALTH) {
            s_health_in_flight = true;
        }
    }
    memset(&job, 0, sizeof(job));
}

/* ---- inbound messages ---- */

static void on_register_ack(const proto_register_ack_t *ack, uint32_t now)
{
    if (s_info.ws_phase != APP_WS_PHASE_RUNNING || !s_ws_connected) {
        return;
    }
    if (ack->proto != PROTO_VERSION) {
        link_lost(now, "Server version mismatch", UPDATE_RETRY_MS);
        return;
    }
    s_info.ws = APP_WS_REGISTERED;
    s_info.mode = ack->mode;
    s_info.server_caps = ack->caps;
    s_info.rejected_401 = 0;
    s_info.retry_at_ms = 0;
    s_info.last_error[0] = '\0';
    backoff_reset(&s_ws_backoff);
    s_registered_this_boot = true;
    if (ack->name[0] != '\0' && strcmp(ack->name, s_info.name) != 0) {
        strlcpy(s_info.name, ack->name, sizeof(s_info.name));
        storage_set_dev_name(ack->name);
    }
    app_clock_on_time(ack->server_time_ms, ack->has_offset, ack->utc_offset_min);
    set_state(APP_STATE_ONLINE);
    ESP_LOGI(TAG, "registered");
    app_telemetry_on_registered(now);
    app_pets_on_registered(now);
}

static void handle_message(const char *json, size_t len, uint32_t now)
{
    if (!proto_parse(json, len, &s_msg)) {
        s_info.rx_invalid++;
        ESP_LOGD(TAG, "invalid frame of %u bytes", (unsigned)len);
        return;
    }
    /* Length and CRC-32 let a test prove that a reassembled frame equals the one the server sent. */
    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"rx\",\"type\":\"%s\",\"bytes\":%u,\"crc32\":\"%08lx\"}",
             proto_type_name(s_msg.type), (unsigned)len,
             (unsigned long)esp_rom_crc32_le(0, (const uint8_t *)json, len));
    console_evt(evt);

    switch (s_msg.type) {
    case PROTO_REGISTER_ACK:
        on_register_ack(&s_msg.u.register_ack, now);
        break;
    case PROTO_PING:
        link_send(now, "pong", proto_write_pong(s_tx, sizeof(s_tx)), NULL);
        break;
    case PROTO_ERROR:
        if (strcmp(s_msg.u.error.code, "UNSUPPORTED_PROTO") == 0) {
            ESP_LOGW(TAG, "server error %s", s_msg.u.error.code);
            s_unsupported_proto = true;
        }
        app_flow_on_message(&s_msg, now);
        break;
    case PROTO_MODE:
        s_info.mode = s_msg.u.mode;
        break;
    case PROTO_STATUS:
    case PROTO_NOTIFY:
    case PROTO_MOOD:
    case PROTO_PERMISSION:
    case PROTO_QUESTION:
    case PROTO_PLAN:
    case PROTO_DEADLINE:
    case PROTO_DISMISS:
    case PROTO_IDENTIFY:
        app_flow_on_message(&s_msg, now);
        break;
    case PROTO_PET_CMD:
        app_pets_on_cmd(&s_msg.u.pet_cmd, now);
        break;
    case PROTO_TIME:
        app_clock_on_time(s_msg.u.time.server_time_ms, true, s_msg.u.time.utc_offset_min);
        break;
    case PROTO_UNKNOWN:
    case PROTO_PONG:
    case PROTO_PEERS:
        break;
    }
}

/* ---- WebSocket events ---- */

static void on_ws_up(uint32_t now)
{
    if (s_info.ws_phase != APP_WS_PHASE_RUNNING) {
        return;
    }
    s_ws_connected = true;
    s_info.ws = APP_WS_CONNECTED;
    s_last_rx_ms = now;
    s_info.server_caps = 0;
    const int len = proto_write_register(s_tx, sizeof(s_tx), s_device_id, APP_MODEL,
                                         esp_app_get_description()->version,
                                         PROTO_CAP_DECIDE | PROTO_CAP_ASK | PROTO_CAP_PLAN | PROTO_CAP_PETS);
    link_send(now, "register", len, NULL);
}

static void on_ws_down(uint16_t close_code, uint16_t http_status, uint32_t now)
{
    if (s_info.ws_phase != APP_WS_PHASE_RUNNING) {
        return;
    }
    ESP_LOGW(TAG, "socket down, close %u, http %u", close_code, http_status);
    if (close_code == CLOSE_REVOKED) {
        request_stop(now);
        forget_pairing();
        set_error("Pairing revoked");
        enter_pairing("Pairing revoked");
        return;
    }
    if (http_status == HTTP_UNAUTHORIZED) {
        /* Whether this 401 counts is decided by the health check; the retry is scheduled from its result. */
        request_stop(now);
        s_info.retry_at_ms = 0;
        s_want_health = true;
        return;
    }
    s_info.rejected_401 = 0;
    if (close_code == CLOSE_UNSUPPORTED || s_unsupported_proto) {
        link_lost(now, "Update required", UPDATE_RETRY_MS);
        return;
    }
    char why[LINE_SIZE];
    if (close_code != 0) {
        snprintf(why, sizeof(why), "Server closed (%u)", close_code);
    } else if (http_status != 0) {
        snprintf(why, sizeof(why), "Server refused (HTTP %u)", http_status);
    } else if (s_ws_connected) {
        snprintf(why, sizeof(why), "Connection lost");
    } else {
        snprintf(why, sizeof(why), "Server unreachable");
    }
    link_lost(now, why, 0);
}

static void on_ws_stopped(uint32_t now)
{
    if (s_info.ws_phase != APP_WS_PHASE_STOPPING) {
        return;
    }
    s_info.ws_phase = APP_WS_PHASE_IDLE;
    s_stop_retry_at = 0;
    /* Reported before a new client starts, so observers see every step of the lifecycle. */
    app_publish_link(&s_info);
    if (s_want_connect && is_server_state()) {
        try_connect(now);
    }
    s_want_connect = false;
}

/* ---- WiFi and provisioning events ---- */

static void finish_trial(bool ok, const char *message)
{
    s_trial.active = false;
    memset(s_trial.psk, 0, sizeof(s_trial.psk));
    const app_request_t *req = app_request_current();
    if (req != NULL && req->kind == APP_REQUEST_WIFI) {
        app_request_complete(ok, message);
    }
}

static void on_wifi_up(uint32_t now)
{
    const bool was_up = s_wifi_up;
    s_wifi_up = true;
    s_wifi_failures = 0;
    s_wifi_retry_at = 0;
    backoff_reset(&s_wifi_backoff);
    s_info.wifi = s_state == APP_STATE_PROVISIONING ? APP_WIFI_PROVISIONING : APP_WIFI_CONNECTED;

    if (s_trial.active) {
        strlcpy(s_ssid, s_trial.ssid, sizeof(s_ssid));
        strlcpy(s_psk, s_trial.psk, sizeof(s_psk));
        strlcpy(s_info.ssid, s_ssid, sizeof(s_info.ssid));
        const esp_err_t err = storage_set_wifi(s_ssid, s_psk);
        finish_trial(true, err == ESP_OK ? "" : "connected; not saved (storage error)");
    }
    /* During provisioning the manager decides when it is over; a repeated address changes nothing. */
    if (s_state == APP_STATE_PROVISIONING || was_up) {
        return;
    }
    on_wifi_ready(now);
}

static void on_wifi_down(net_wifi_fail_t fail, uint32_t now)
{
    const bool was_up = s_wifi_up;
    s_wifi_up = false;
    s_wifi_fail = fail;
    if (s_state == APP_STATE_PROVISIONING) {
        /* The provisioning manager reconnects by itself (network_provisioning manager.c). */
        return;
    }
    if (s_info.wifi == APP_WIFI_INIT_FAILED || s_info.wifi == APP_WIFI_NO_CREDENTIALS ||
        (s_ssid[0] == '\0' && !s_trial.active)) {
        return;
    }
    if (was_up) {
        new_generation();
        request_stop(now);
        s_info.retry_at_ms = 0;
        s_wifi_failures = 0;
        if (s_want_claim) {
            s_want_claim = false;
            app_request_complete(false, "WiFi connection lost");
        }
    }
    if (fail == NET_WIFI_FAIL_LEFT) {
        /* wifi_connect() dropped the old connection and is already trying the next one. */
        if (was_up) {
            s_info.wifi = APP_WIFI_CONNECTING;
            set_state(APP_STATE_WIFI_CONNECTING);
        }
        return;
    }
    if (!was_up) {
        s_wifi_failures++;
    }
    s_info.wifi = APP_WIFI_FAILED;
    set_state(was_up ? APP_STATE_WIFI_CONNECTING : APP_STATE_WIFI_FAILED);
    s_wifi_retry_at = after(now, backoff_next(&s_wifi_backoff));
}

static void wifi_attempt(uint32_t now)
{
    s_wifi_retry_at = 0;
    if (s_suspended) {
        return;
    }
    const char *ssid = s_trial.active ? s_trial.ssid : s_ssid;
    const char *psk = s_trial.active ? s_trial.psk : s_psk;
    const esp_err_t err = wifi_connect(ssid, psk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi connect: %s", esp_err_to_name(err));
        s_wifi_fail = NET_WIFI_FAIL_OTHER;
        s_wifi_failures++;
        s_info.wifi = APP_WIFI_FAILED;
        set_state(APP_STATE_WIFI_FAILED);
        s_wifi_retry_at = after(now, backoff_next(&s_wifi_backoff));
        return;
    }
    s_info.wifi = APP_WIFI_CONNECTING;
    if (!s_wifi_up) {
        set_state(APP_STATE_WIFI_CONNECTING);
    }
}

static void start_provisioning(void)
{
    /* Cleared first: a reset during provisioning returns to the stored credentials. */
    storage_set_prov_request(false);
    const esp_err_t err = prov_start(on_net_event, &s_prov);
    if (err != ESP_OK) {
        set_error("WiFi setup failed to start");
        s_info.wifi = APP_WIFI_INIT_FAILED;
        set_state(APP_STATE_WIFI_FAILED);
        return;
    }
    s_prov_status[0] = '\0';
    s_info.wifi = APP_WIFI_PROVISIONING;
    set_state(APP_STATE_PROVISIONING);
}

static void on_prov_event(net_event_id_t id, net_wifi_fail_t fail, uint32_t now)
{
    if (s_state != APP_STATE_PROVISIONING) {
        return;
    }
    switch (id) {
    case NET_EV_PROV_CRED_RECV:
        strlcpy(s_prov_status, "Connecting to WiFi...", sizeof(s_prov_status));
        break;
    case NET_EV_PROV_CRED_FAIL:
        prov_retry_after_failure();
        strlcpy(s_prov_status, fail == NET_WIFI_FAIL_WRONG_PASSWORD ? "Wrong password, try again"
                                                                    : "Network not found, try again",
                sizeof(s_prov_status));
        break;
    case NET_EV_PROV_CRED_SUCCESS:
        /* The station has an address with these credentials, so they are worth keeping. */
        if (prov_get_credentials(s_ssid, s_psk)) {
            strlcpy(s_info.ssid, s_ssid, sizeof(s_info.ssid));
            const esp_err_t err = storage_set_wifi(s_ssid, s_psk);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "credentials not saved: %s", esp_err_to_name(err));
            }
        }
        strlcpy(s_prov_status, "Connected", sizeof(s_prov_status));
        break;
    case NET_EV_PROV_END:
        prov_finish();
        /* The phone may have set another server through the custom-data endpoint. */
        load_server_url();
        if (s_wifi_up) {
            s_info.wifi = APP_WIFI_CONNECTED;
            on_wifi_ready(now);
        } else if (s_ssid[0] != '\0') {
            wifi_attempt(now);
        } else {
            set_error("WiFi setup ended without credentials");
            s_info.wifi = APP_WIFI_OFF;
            set_state(APP_STATE_WIFI_FAILED);
        }
        break;
    default:
        break;
    }
}

/* ---- console requests ---- */

static void handle_request(app_request_t *req, uint32_t now)
{
    switch (req->kind) {
    case APP_REQUEST_SERVER: {
        url_t url;
        char base[URL_BASE_SIZE];
        if (!url_parse(req->text, &url) || url_base(&url, base, sizeof(base)) < 0) {
            app_request_complete(false, "expected ws://host[:port] or wss://host[:port]");
            return;
        }
        const esp_err_t err = storage_set_server_url(base);
        reconnect_now(now);
        if (err != ESP_OK) {
            /* Degraded storage: the URL applies until the next reset. */
            strlcpy(s_url_text, base, sizeof(s_url_text));
            strlcpy(s_info.server, base, sizeof(s_info.server));
            s_url = url;
            s_url_valid = true;
        }
        app_request_complete(true, err == ESP_OK ? "" : "in use; not saved (storage error)");
        return;
    }
    case APP_REQUEST_PAIR:
        if (!s_wifi_up) {
            app_request_complete(false, "WiFi is not connected");
            return;
        }
        if (!pair_code_normalise(req->text, s_claim_code)) {
            app_request_complete(false, "not a pairing code");
            return;
        }
        s_want_claim = true;
        return;
    case APP_REQUEST_WIFI:
        if (s_info.wifi == APP_WIFI_INIT_FAILED || s_state == APP_STATE_PROVISIONING) {
            app_request_complete(false, s_state == APP_STATE_PROVISIONING ? "provisioning is running"
                                                                          : "WiFi init failed");
            return;
        }
        strlcpy(s_trial.ssid, req->text, sizeof(s_trial.ssid));
        strlcpy(s_trial.psk, req->arg, sizeof(s_trial.psk));
        s_trial.active = true;
        s_trial.deadline = after(now, WIFI_TRIAL_MS);
        backoff_reset(&s_wifi_backoff);
        wifi_attempt(now);
        return;
    case APP_REQUEST_MSG:
        handle_message(req->text, strlen(req->text), now);
        app_request_complete(true, "");
        return;
    case APP_REQUEST_RESET_PAIRING:
        forget_pairing();
        reconnect_now(now);
        app_request_complete(true, "");
        return;
    case APP_REQUEST_PAIR_RESTORE: {
        const esp_err_t err = storage_restore_pairing();
        if (err != ESP_OK) {
            app_request_complete(false, err == ESP_ERR_NOT_FOUND ? "no backup" : esp_err_to_name(err));
            return;
        }
        s_info.paired = storage_get_pairing(s_token, NULL);
        reconnect_now(now);
        app_request_complete(true, "");
        return;
    }
    case APP_REQUEST_PREVIEW:
        if (strcmp(req->text, "pairing") == 0) {
            s_preview = PREVIEW_PAIRING;
        } else if (strcmp(req->text, "provisioning") == 0) {
            s_preview = PREVIEW_PROVISIONING;
        } else if (strcmp(req->text, "approval-low") == 0) {
            s_preview = PREVIEW_APPROVAL_LOW;
        } else if (strcmp(req->text, "approval-high") == 0) {
            s_preview = PREVIEW_APPROVAL_HIGH;
        } else if (strcmp(req->text, "identify") == 0) {
            s_preview = PREVIEW_IDENTIFY;
        } else if (strcmp(req->text, "settings") == 0) {
            s_preview = PREVIEW_SETTINGS;
        } else if (strcmp(req->text, "off") == 0) {
            s_preview = PREVIEW_OFF;
        } else {
            app_request_complete(false, "unknown preview screen");
            return;
        }
        s_info.preview = s_preview != PREVIEW_OFF;
        app_request_complete(true, "");
        return;
    case APP_REQUEST_RENDER:
        s_screen_valid = false;
        app_request_complete(true, "");
        return;
    case APP_REQUEST_FACTORY_RESET:
        app_request_complete(true, "restarting");
        app_factory_reset();
        return;
    case APP_REQUEST_POWER:
        app_power_on_request(req);
        return;
    case APP_REQUEST_CLOCK:
        app_clock_on_request(req);
        return;
    case APP_REQUEST_SHOWCASE:
        app_flow_showcase_on_request(req, now);
        s_screen_valid = false;
        return;
    case APP_REQUEST_SETTINGS:
        app_settings_on_request(req, now);
        s_screen_valid = false;
        return;
    case APP_REQUEST_PET: {
        const pet_result_t r =
            strcmp(req->text, "use") == 0 ? app_pets_select(req->arg, now) : app_pets_remove(req->arg, now);
        app_request_complete(r == PET_RESULT_OK, pet_result_name(r));
        if (r == PET_RESULT_OK) {
            s_screen_valid = false;
        }
        return;
    }
    }
}

/* ---- deadlines ---- */

static void run_deadlines(uint32_t now)
{
    if (due(s_stop_retry_at, now)) {
        s_stop_submitted = ws_request_stop();
        s_stop_retry_at = s_stop_submitted ? 0 : after(now, STOP_RESUBMIT_MS);
    }
    if (s_info.ws_phase == APP_WS_PHASE_STOPPING && !s_stop_warned && now - s_stop_since > STOP_WARN_MS) {
        s_stop_warned = true;
        ESP_LOGW(TAG, "websocket client still stopping after %d s", STOP_WARN_MS / 1000);
    }
    /*
     * Also before the upgrade: an attempt whose up or down event could not be
     * queued must not hold the link for ever, and every stage of a connect
     * gives up long before this.
     */
    if (s_info.ws_phase == APP_WS_PHASE_RUNNING && now - s_last_rx_ms > LINK_SILENCE_MS) {
        link_lost(now, "Server silent", 0);
    }
    if (due(s_info.retry_at_ms, now)) {
        s_info.retry_at_ms = 0;
        if (is_server_state()) {
            try_connect(now);
        }
    }

    if (s_trial.active && due(s_trial.deadline, now)) {
        finish_trial(false, "could not connect; previous credentials restored");
        backoff_reset(&s_wifi_backoff);
        s_wifi_failures = 0;
        if (s_ssid[0] != '\0') {
            wifi_attempt(now);
        } else {
            s_wifi_retry_at = 0;
            s_info.wifi = storage_is_degraded() ? APP_WIFI_NO_CREDENTIALS : APP_WIFI_OFF;
            set_state(APP_STATE_WIFI_FAILED);
        }
    }
    if (due(s_wifi_retry_at, now)) {
        wifi_attempt(now);
    }

    if (s_state == APP_STATE_PAIRING && s_wifi_up) {
        if (s_pair.phase == PAIR_IDLE || due(s_pair.retry_at, now)) {
            pair_request_code();
        } else if (s_pair.phase == PAIR_SHOWING && due(s_pair.expires_at, now)) {
            pair_request_code();
        } else if (s_pair.phase == PAIR_SHOWING && due(s_pair.next_poll_at, now)) {
            s_pair.next_poll_at = 0;
            s_want_pair_poll = true;
        }
    }
    submit_next_job();
    app_settings_run_deadlines(now);
}

static uint32_t earliest(uint32_t a, uint32_t b, uint32_t now)
{
    if (a == 0) {
        return b;
    }
    if (b == 0) {
        return a;
    }
    return (int32_t)(a - now) < (int32_t)(b - now) ? a : b;
}

static void arm_timer(uint32_t now)
{
    uint32_t at = earliest(s_info.retry_at_ms, s_wifi_retry_at, now);
    at = earliest(at, s_stop_retry_at, now);
    if (s_trial.active) {
        at = earliest(at, s_trial.deadline, now);
    }
    if (s_state == APP_STATE_PAIRING) {
        at = earliest(at, s_pair.next_poll_at, now);
        at = earliest(at, s_pair.retry_at, now);
    }
    at = earliest(at, app_settings_deadline(), now);
    esp_timer_stop(s_timer);
    if (at == 0) {
        return;
    }
    const int32_t ms = (int32_t)(at - now);
    esp_timer_start_once(s_timer, ms > 1 ? (uint64_t)ms * 1000 : 1000);
}

/* ---- screens ---- */

static void show(ui_screen_id_t id)
{
    if (!s_screen_valid || s_screen != id) {
        s_screen_valid = true;
        s_screen = id;
        app_show_screen(id);
    }
}

static const char *wifi_fail_text(void)
{
    if (s_wifi_failures == 0) {
        return "";
    }
    switch (s_wifi_fail) {
    case NET_WIFI_FAIL_WRONG_PASSWORD:
        return "Wrong password";
    case NET_WIFI_FAIL_NOT_FOUND:
        return "Network not found";
    case NET_WIFI_FAIL_OTHER:
    case NET_WIFI_FAIL_LEFT:
        break;
    }
    return "Connection failed";
}

static void render_wifi(uint32_t now)
{
    char retry[LINE_SIZE] = "";
    const char *state = "Joining Wi-Fi";
    const char *reason = wifi_fail_text();
    switch (s_info.wifi) {
    case APP_WIFI_INIT_FAILED:
        state = "WiFi init failed";
        reason = s_info.last_error;
        break;
    case APP_WIFI_NO_CREDENTIALS:
        state = "Storage error - see console";
        reason = "";
        break;
    case APP_WIFI_OFF:
        state = "Not configured";
        reason = s_info.last_error;
        break;
    case APP_WIFI_FAILED:
        state = "Not connected";
        break;
    case APP_WIFI_PROVISIONING:
    case APP_WIFI_CONNECTING:
    case APP_WIFI_CONNECTED:
        break;
    }
    if (s_wifi_retry_at != 0) {
        snprintf(retry, sizeof(retry), "retry in %lu s", (unsigned long)seconds_until(s_wifi_retry_at, now));
    }
    ui_wifi_set(s_trial.active ? s_trial.ssid : s_ssid, state, reason, retry, s_wifi_failures >= WIFI_HINT_FAILURES);
    show(UI_SCREEN_WIFI);
}

static void render_pairing(uint32_t now)
{
    char error[LINE_SIZE + 24];
    if (s_pair.error[0] != '\0' && (s_pair.retry_at != 0 || s_pair.next_poll_at != 0)) {
        const uint32_t at = s_pair.retry_at != 0 ? s_pair.retry_at : s_pair.next_poll_at;
        snprintf(error, sizeof(error), "%s, retry in %lu s", s_pair.error, (unsigned long)seconds_until(at, now));
    } else if (s_pair.error[0] != '\0') {
        strlcpy(error, s_pair.error, sizeof(error));
    } else {
        strlcpy(error, s_pair.banner, sizeof(error));
    }
    ui_pairing_set(s_pair.code, s_url_text, error, seconds_until(s_pair.expires_at, now), s_pair.total_s);
    show(UI_SCREEN_PAIRING);
}

static void render_connecting(uint32_t now)
{
    char retry[LINE_SIZE] = "";
    if (s_info.retry_at_ms != 0) {
        snprintf(retry, sizeof(retry), "retry in %lu s", (unsigned long)seconds_until(s_info.retry_at_ms, now));
    }
    const char *state = "Waiting";
    if (s_info.ws == APP_WS_CONNECTING) {
        state = "Connecting\xE2\x80\xA6";
    } else if (s_info.ws == APP_WS_CONNECTED) {
        state = "Registering\xE2\x80\xA6";
    }
    ui_connecting_set(s_url_text, state, s_info.last_error, retry);
    show(UI_SCREEN_CONNECTING);
}

static const char *offline_banner(uint32_t now, char buf[LINE_SIZE])
{
    if (s_state == APP_STATE_ONLINE) {
        return NULL;
    }
    if (s_deep_wake && !s_registered_this_boot && s_info.last_error[0] == '\0' && s_wifi_failures == 0) {
        return "Reconnecting";
    }
    if (!s_wifi_up) {
        return "No WiFi";
    }
    if (strcmp(s_info.last_error, "Server rejected token, retrying") == 0) {
        return s_info.last_error;
    }
    if (strcmp(s_info.last_error, "Incompatible server") == 0 || strcmp(s_info.last_error, "Update required") == 0 ||
        strcmp(s_info.last_error, "Server version mismatch") == 0) {
        return "Incompatible server";
    }
    if (s_info.retry_at_ms != 0) {
        snprintf(buf, LINE_SIZE, "Server offline, retry in %lus", (unsigned long)seconds_until(s_info.retry_at_ms, now));
        return buf;
    }
    return "Server offline";
}

static uint8_t rssi_bars(void)
{
    if (!s_wifi_up) {
        return 0;
    }
    wifi_status_t w;
    wifi_get_status(&w);
    if (!w.connected) {
        return 0;
    }
    if (w.rssi >= -60) {
        return 4;
    }
    if (w.rssi >= -70) {
        return 3;
    }
    return w.rssi >= -80 ? 2 : 1;
}

static void render_status_bar(void)
{
    power_battery_t b;
    power_get_battery(&b);
    ui_status_t st = {
        .pct = b.pct > 0 ? (uint8_t)(b.pct > 100 ? 100 : b.pct) : 0,
        .low = b.low,
        .desk = s_info.mode == PROTO_MODE_DESK,
        .rssi_bars = rssi_bars(),
        .link = UI_LINK_OFFLINE,
    };
    switch (b.state) {
    case BATTERY_STATE_ABSENT:
        st.battery = UI_BATT_ABSENT;
        break;
    case BATTERY_STATE_FULL:
        st.battery = UI_BATT_FULL;
        break;
    case BATTERY_STATE_CHARGING:
        st.battery = UI_BATT_CHARGING;
        break;
    case BATTERY_STATE_DISCHARGING:
        st.battery = UI_BATT_DISCHARGING;
        break;
    }
    if (!b.valid) {
        st.battery = UI_BATT_ABSENT;
    }
    if (s_info.ws == APP_WS_REGISTERED) {
        st.link = UI_LINK_REGISTERED;
    } else if (s_info.ws_phase == APP_WS_PHASE_RUNNING) {
        st.link = UI_LINK_CONNECTING;
    }
    if (app_flow_showcase_active()) {
        st.link = UI_LINK_HIDDEN;
    }
    char date[CLOCK_DATE_SIZE];
    app_clock_format(app_clock_epoch_ms(), st.time, date);
    ui_status_set(&st);
}

static void render_flow(uint32_t now, app_flow_preview_t preview)
{
    char buf[LINE_SIZE];
    /* The showcase is a local demo: a missing server is not news to the audience. */
    const bool banner = preview == APP_FLOW_PREVIEW_OFF && !app_flow_showcase_active();
    const ui_screen_id_t id = app_flow_render(now, banner ? offline_banner(now, buf) : NULL,
                                              preview);
    s_flow_shown = preview == APP_FLOW_PREVIEW_OFF;
    show(id);
}

static void render(uint32_t now)
{
    if (!s_splash_done || app_power_screen_off()) {
        return;
    }
    if (app_power_lowbatt()) {
        s_flow_shown = false;
        app_flow_hide_banner();
        show(UI_SCREEN_LOWBATT);
        return;
    }
    render_status_bar();
    uint32_t sleep_ms;
    /* A request, a question or identify outranks the warning (ux-flows 1), and none can be queued during it. */
    if (app_power_sleep_warning(&sleep_ms) && !app_flow_takes_over(now)) {
        s_flow_shown = false;
        app_flow_hide_banner();
        ui_sleep_set((uint8_t)((sleep_ms + 999) / 1000), (uint16_t)(sleep_ms * 1000 / POWER_FSM_SLEEP_WARN_MS));
        show(UI_SCREEN_SLEEP);
        return;
    }
    s_flow_shown = false;
    switch (s_preview) {
    case PREVIEW_PAIRING:
        ui_pairing_set("K7M2QX", "ws://devotchi.example:8080", "", 212, CODE_DEFAULT_LIFE_S);
        show(UI_SCREEN_PAIRING);
        return;
    case PREVIEW_PROVISIONING:
        ui_provisioning_set("DEVOTCHI-0000", "SAMPLE23", "");
        show(UI_SCREEN_PROVISIONING);
        return;
    case PREVIEW_APPROVAL_LOW:
        render_flow(now, APP_FLOW_PREVIEW_APPROVAL_LOW);
        return;
    case PREVIEW_APPROVAL_HIGH:
        render_flow(now, APP_FLOW_PREVIEW_APPROVAL_HIGH);
        return;
    case PREVIEW_IDENTIFY:
        render_flow(now, APP_FLOW_PREVIEW_IDENTIFY);
        return;
    case PREVIEW_SETTINGS:
        app_settings_render(true);
        show(UI_SCREEN_SETTINGS);
        return;
    case PREVIEW_OFF:
        break;
    }
    /* The showcase is local: it shows whatever the link is doing. */
    if (app_flow_showcase_active() && !app_settings_active()) {
        render_flow(now, APP_FLOW_PREVIEW_OFF);
        return;
    }
    if (app_settings_active()) {
        if (app_flow_takes_over(now)) {
            render_flow(now, APP_FLOW_PREVIEW_OFF);
        } else {
            if (app_flow_showcase_active()) {
                app_flow_drop_offline_banner();
            }
            app_settings_render(false);
            show(app_settings_picker() ? UI_SCREEN_PICKER : UI_SCREEN_SETTINGS);
        }
        return;
    }
    switch (s_state) {
    case APP_STATE_BOOT:
        break;
    case APP_STATE_PROVISIONING:
        ui_provisioning_set(s_prov.service_name, s_prov.pop, s_prov_status);
        show(UI_SCREEN_PROVISIONING);
        break;
    case APP_STATE_WIFI_CONNECTING:
    case APP_STATE_WIFI_FAILED:
        /* After the first registration of a boot the pet stays up with an offline banner. */
        if (s_registered_this_boot || s_deep_wake) {
            render_flow(now, APP_FLOW_PREVIEW_OFF);
        } else {
            render_wifi(now);
        }
        break;
    case APP_STATE_PAIRING:
        render_pairing(now);
        break;
    case APP_STATE_SERVER_CONNECTING:
    case APP_STATE_ONLINE:
    case APP_STATE_OFFLINE:
        if (s_registered_this_boot || s_deep_wake) {
            render_flow(now, APP_FLOW_PREVIEW_OFF);
        } else {
            render_connecting(now);
        }
        break;
    }
}

/* ---- entry points ---- */

static bool startup_screen(void)
{
    return s_screen_valid && (s_screen == UI_SCREEN_WIFI || s_screen == UI_SCREEN_CONNECTING ||
                              s_screen == UI_SCREEN_PAIRING || s_screen == UI_SCREEN_PROVISIONING);
}

btn_mode_t app_link_btn_mode(btn_id_t id, uint32_t now)
{
    if (s_preview != PREVIEW_OFF) {
        return BTN_MODE_NONE;
    }
    if (app_settings_active() && s_screen_valid && s_screen == UI_SCREEN_PICKER) {
        return BTN_MODE_CLICK;
    }
    if (app_settings_active() && s_screen_valid && s_screen == UI_SCREEN_SETTINGS) {
        return app_settings_btn_mode(id);
    }
    if (s_flow_shown) {
        return app_flow_btn_mode(id, now);
    }
    /* ACTION hold opens Settings from every start-up screen (ux-flows 2). */
    return id == BTN_ACTION && startup_screen() ? BTN_MODE_NAV_HOLD : BTN_MODE_CLICK;
}

void app_link_on_button(const btn_fsm_out_t *out)
{
    if (s_preview != PREVIEW_OFF) {
        return;
    }
    const uint32_t now = app_now_ms();
    /* A hold's fill moves the tab only; the screen is redrawn when the hold ends. */
    if (out->kind == BTN_PROGRESS) {
        if (s_flow_shown) {
            app_flow_on_button(out, now);
        }
        return;
    }
    const bool settings_long = out->id == BTN_ACTION && out->kind == BTN_HOLD;
    if (app_settings_active() && s_screen_valid &&
        (s_screen == UI_SCREEN_SETTINGS || s_screen == UI_SCREEN_PICKER)) {
        app_settings_on_button(out, now);
    } else if (s_flow_shown) {
        if (!app_flow_on_button(out, now) && settings_long && s_screen == UI_SCREEN_PET) {
            app_settings_open(now, SETTINGS_LIST_QUICK);
        }
    } else if (settings_long && startup_screen()) {
        app_settings_open(now, SETTINGS_LIST_TOP);
    } else if (out->kind != BTN_CLICK) {
        return;
    } else if (s_state == APP_STATE_PROVISIONING && out->id == BTN_ACTION) {
        ui_provisioning_set_step(ui_provisioning_step() == 0 ? 1 : 0);
    } else if (s_state == APP_STATE_PROVISIONING && out->id == BTN_REJECT && ui_provisioning_step() == 1) {
        ui_provisioning_set_step(0);
    } else if (s_state == APP_STATE_PAIRING && out->id == BTN_ACTION && s_pair.phase != PAIR_STARTING && s_wifi_up) {
        backoff_reset(&s_pair.backoff);
        pair_request_code();
    } else if ((s_state == APP_STATE_WIFI_FAILED || s_state == APP_STATE_WIFI_CONNECTING) &&
               out->id == BTN_APPROVE && s_wifi_failures >= WIFI_HINT_FAILURES) {
        if (storage_set_prov_request(true) == ESP_OK) {
            ESP_LOGI(TAG, "restarting into WiFi setup");
            esp_restart();
        }
    } else {
        return;
    }
    run_deadlines(now);
    app_flow_run_deadlines(now);
    app_publish_link(&s_info);
    render(now);
    arm_timer(now);
    app_flow_arm_timer(now);
}

bool app_link_online(void)
{
    return s_info.ws == APP_WS_REGISTERED;
}

void app_link_unpair(uint32_t now)
{
    forget_pairing();
    reconnect_now(now);
}

void app_link_render(uint32_t now)
{
    render(now);
}

void app_link_suspend(uint32_t now)
{
    s_suspended = true;
    s_want_connect = false;
    request_stop(now);
}

bool app_link_ws_idle(void)
{
    return s_info.ws_phase == APP_WS_PHASE_IDLE;
}

bool app_link_handle_event(const app_event_t *ev)
{
    const uint32_t now = app_now_ms();
    switch (ev->id) {
    case APP_EV_TICK:
        s_splash_done = true;
        app_telemetry_tick(now);
        break;
    case APP_EV_LINK_TIMER:
    case APP_EV_FLOW_TIMER:
        break;
    case APP_EV_WIFI_UP:
        on_wifi_up(now);
        break;
    case APP_EV_WIFI_DOWN:
        on_wifi_down((net_wifi_fail_t)ev->arg.u8[0], now);
        break;
    case APP_EV_PROV:
        on_prov_event((net_event_id_t)ev->arg.u8[0], (net_wifi_fail_t)ev->arg.u8[1], now);
        break;
    case APP_EV_WS_UP:
        on_ws_up(now);
        break;
    case APP_EV_WS_MSG: {
        net_ws_msg_t *msg = ev->arg.ptr;
        if (s_info.ws_phase == APP_WS_PHASE_RUNNING) {
            s_last_rx_ms = now;
            handle_message(msg->data, msg->len, now);
        }
        ws_msg_release(msg);
        break;
    }
    case APP_EV_WS_DOWN:
        on_ws_down((uint16_t)(ev->arg.u32 >> 16), (uint16_t)(ev->arg.u32 & 0xFFFF), now);
        break;
    case APP_EV_WS_STOPPED:
        on_ws_stopped(now);
        break;
    case APP_EV_NET_JOB:
        on_job_result(ev->arg.ptr, now);
        break;
    case APP_EV_REQUEST: {
        app_request_t *req = app_request_current();
        if (req != NULL) {
            handle_request(req, now);
        }
        break;
    }
    case APP_EV_BTN_RAW:
    case APP_EV_BTN_TICK:
    case APP_EV_USER_ACTIVITY:
    case APP_EV_FB_STEP:
    case APP_EV_POWER:
    case APP_EV_SETTING_CHANGED:
        return false;
    }
    run_deadlines(now);
    app_flow_run_deadlines(now);
    app_publish_link(&s_info);
    render(now);
    arm_timer(now);
    app_flow_arm_timer(now);
    return true;
}

esp_err_t app_link_init(void)
{
    backoff_init(&s_wifi_backoff, WIFI_BACKOFF_MIN_MS, WIFI_BACKOFF_MAX_MS, rand_fn, NULL);
    backoff_init(&s_ws_backoff, WS_BACKOFF_MIN_MS, WS_BACKOFF_MAX_MS, rand_fn, NULL);
    backoff_init(&s_pair.backoff, PAIR_BACKOFF_MIN_MS, PAIR_BACKOFF_MAX_MS, rand_fn, NULL);
    app_get_device_id(s_device_id);

    const esp_timer_create_args_t timer = {.callback = on_timer, .name = "link"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &s_timer), TAG, "timer");

    const app_task_cfg_t net = APP_TASK_NET;
    const app_task_cfg_t ws = APP_TASK_WS;
    const net_identity_t identity = {
        .device_id = s_device_id,
        .model = APP_MODEL,
        .fw = esp_app_get_description()->version,
    };
    ESP_RETURN_ON_ERROR(net_worker_init(on_net_event, &identity, net.name, net.stack, net.prio), TAG, "net worker");
    ESP_RETURN_ON_ERROR(ws_init(on_net_event, ws.name, ws.stack, ws.prio), TAG, "ws");

    s_wifi_init_err = wifi_init(on_net_event, !storage_is_degraded());
    if (s_wifi_init_err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(s_wifi_init_err));
    }
    return ESP_OK;
}

void app_link_start(void)
{
    const uint32_t now = app_now_ms();
    power_wake_t wake;
    power_get_wake(&wake);
    s_deep_wake = wake.deep_sleep;
    s_splash_done = s_deep_wake;
    load_server_url();
    s_info.paired = storage_get_pairing(s_token, NULL);
    char name[STORAGE_DEV_NAME_SIZE];
    if (storage_get_dev_name(name)) {
        text_utf8_truncate(s_info.name, sizeof(s_info.name), name);
    }
    const bool has_wifi = storage_get_wifi(s_ssid, s_psk);
    strlcpy(s_info.ssid, s_ssid, sizeof(s_info.ssid));

    if (s_wifi_init_err != ESP_OK) {
        set_error(esp_err_to_name(s_wifi_init_err));
        s_info.wifi = APP_WIFI_INIT_FAILED;
        set_state(APP_STATE_WIFI_FAILED);
    } else if (storage_is_degraded() && !has_wifi) {
        /* Provisioning could not save anything; the console command `wifi` supplies credentials for this boot. */
        prov_release_bt_memory();
        s_info.wifi = APP_WIFI_NO_CREDENTIALS;
        set_state(APP_STATE_WIFI_FAILED);
    } else if (storage_get_prov_request() || !has_wifi) {
        start_provisioning();
    } else {
        prov_release_bt_memory();
        wifi_attempt(now);
    }
    app_publish_link(&s_info);
    arm_timer(now);
}
