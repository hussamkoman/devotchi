#include "app_priv.h"

#include "app.h"
#include "battery.h"
#include "power.h"
#include "proto.h"
#include "ui.h"
#include "wifi.h"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Protocol 4.12. */
#define PERIOD_MS     60000
#define PCT_STEP      5
/* Above the server's 1/s limit for telemetry and still inside the 2 s allowed after a change. */
#define MIN_GAP_MS    1500
#define TX_SIZE       (PROTO_TELEMETRY_MAX + 1)

/* App task only. */
static char s_tx[TX_SIZE];
static bool s_sent;
static uint32_t s_sent_ms;
/* The 60 s rhythm runs from the registration; change-driven samples do not move it. */
static uint32_t s_period_ms;
static const char *s_last_state;
static int s_last_pct;
static const char *s_last_alert;

typedef struct {
    const char *state;
    int pct;
    const char *alert;
} battery_view_t;

static void read_battery(power_battery_t *b, battery_view_t *v)
{
    power_get_battery(b);
    const bool present = b->valid && b->state != BATTERY_STATE_ABSENT;
    v->state = present ? battery_state_name(b->state) : "absent";
    v->pct = present ? b->pct : -1;
    v->alert = !present ? NULL : b->critical ? "battery_critical" : b->low ? "battery_low" : NULL;
}

static bool same(const char *a, const char *b)
{
    return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

static void send(uint32_t now, const char *reason)
{
    power_battery_t b;
    battery_view_t v;
    read_battery(&b, &v);
    wifi_status_t w;
    wifi_get_status(&w);
    const proto_telemetry_t t = {
        .battery_state = v.state,
        .pct = v.pct,
        .mv = (int)b.mv,
        /* Without a battery the board can only be running on USB. */
        .ext_power = b.ext_power || v.pct < 0,
        .rssi = w.rssi,
        .uptime_s = app_uptime_s(),
        .heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .heap_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        .reset_reason = app_reset_reason(),
        .fw = esp_app_get_description()->version,
        .screen = ui_current_screen_name(),
        .stage = "active",
        .alert = v.alert,
    };
    const int len = proto_write_telemetry(s_tx, sizeof(s_tx), &t);
    char extra[48];
    snprintf(extra, sizeof(extra), ",\"bytes\":%d,\"reason\":\"%s\"", len, reason);
    app_link_send(now, "telemetry", s_tx, len, extra);
    /* A sample that could not be sent is not retried before the next period or change. */
    s_sent = true;
    s_sent_ms = now;
    s_last_state = v.state;
    s_last_pct = v.pct;
    s_last_alert = v.alert;
}

void app_telemetry_on_registered(uint32_t now)
{
    send(now, "register");
    s_period_ms = now;
}

void app_telemetry_tick(uint32_t now)
{
    if (!s_sent || !app_link_online()) {
        return;
    }
    if (now - s_sent_ms < MIN_GAP_MS) {
        return;
    }
    if (now - s_period_ms >= PERIOD_MS) {
        s_period_ms += PERIOD_MS;
        if (now - s_period_ms >= PERIOD_MS) {
            s_period_ms = now;
        }
        send(now, "period");
        return;
    }
    power_battery_t b;
    battery_view_t v;
    read_battery(&b, &v);
    const bool pct_step = (v.pct < 0) != (s_last_pct < 0) || abs(v.pct - s_last_pct) >= PCT_STEP;
    if (!same(v.state, s_last_state) || pct_step || !same(v.alert, s_last_alert)) {
        send(now, "battery");
    }
}
