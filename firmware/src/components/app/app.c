#include "app.h"

#include "app_priv.h"
#include "app_tasks.h"
#include "console.h"
#include "storage.h"
#include "ui.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "app";

#define POST_TIMEOUT_MS      10
#define SNAPSHOT_TIMEOUT_MS  100
#define TICK_PERIOD_US       1000000

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_snapshot_lock;
static esp_timer_handle_t s_tick_timer;

/* Written by the app task under s_snapshot_lock; other tasks read it through app_get_snapshot(). */
static app_snapshot_t s_snapshot;

const char *app_state_name(app_state_t state)
{
    switch (state) {
    case APP_STATE_BOOT:
        return "boot";
    case APP_STATE_PROVISIONING:
        return "provisioning";
    case APP_STATE_WIFI_CONNECTING:
        return "wifi_connecting";
    case APP_STATE_WIFI_FAILED:
        return "wifi_failed";
    case APP_STATE_PAIRING:
        return "pairing";
    case APP_STATE_SERVER_CONNECTING:
        return "server_connecting";
    case APP_STATE_ONLINE:
        return "online";
    case APP_STATE_OFFLINE:
        return "offline";
    }
    return "";
}

const char *app_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_SW:
        return "sw";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_USB:
        return "usb";
    default:
        return "other";
    }
}

void app_get_device_id(char id[APP_DEVICE_ID_SIZE])
{
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(id, APP_DEVICE_ID_SIZE, "dv-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

uint32_t app_uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

bool app_post(const app_event_t *ev)
{
    if (xQueueSend(s_queue, ev, pdMS_TO_TICKS(POST_TIMEOUT_MS)) == pdTRUE) {
        return true;
    }
    if (xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(POST_TIMEOUT_MS)) == pdTRUE) {
        s_snapshot.events_dropped++;
        xSemaphoreGive(s_snapshot_lock);
    }
    return false;
}

bool app_post_from_isr(const app_event_t *ev)
{
    BaseType_t woken = pdFALSE;
    const bool ok = xQueueSendFromISR(s_queue, ev, &woken) == pdTRUE;
    portYIELD_FROM_ISR(woken);
    return ok;
}

void app_get_snapshot(app_snapshot_t *out)
{
    *out = (app_snapshot_t){0};
    if (xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(SNAPSHOT_TIMEOUT_MS)) == pdTRUE) {
        *out = s_snapshot;
        xSemaphoreGive(s_snapshot_lock);
    }
}

static void show_screen(ui_screen_id_t id)
{
    if (ui_show_screen(id) != ESP_OK) {
        ESP_LOGE(TAG, "screen %s unavailable", ui_screen_name(id));
        return;
    }
    char evt[64];
    snprintf(evt, sizeof(evt), "{\"evt\":\"screen\",\"id\":\"%s\"}", ui_screen_name(id));
    console_evt(evt);
}

static void fill_diag_screen(void)
{
    char line[STORAGE_URL_SIZE + 16];
    char id[APP_DEVICE_ID_SIZE];
    char url[STORAGE_URL_SIZE];
    storage_url_source_t source;

    app_get_device_id(id);
    snprintf(line, sizeof(line), "fw %s  reset %s", esp_app_get_description()->version, app_reset_reason());
    ui_diag_set_line(0, line);
    snprintf(line, sizeof(line), "id %s", id);
    ui_diag_set_line(1, line);
    snprintf(line, sizeof(line), "nvs %s  wifi %s  paired %s", storage_is_degraded() ? "error" : "ok",
             storage_has_wifi() ? "yes" : "no", storage_is_paired() ? "yes" : "no");
    ui_diag_set_line(2, line);
    storage_get_server_url(url, &source);
    snprintf(line, sizeof(line), "%s (%s)", url, source == STORAGE_URL_SOURCE_NVS ? "nvs" : "default");
    ui_diag_set_line(3, line);
}

static void handle_event(const app_event_t *ev)
{
    switch (ev->id) {
    case APP_EV_TICK:
        if (ui_current_screen() == UI_SCREEN_SPLASH) {
            show_screen(UI_SCREEN_DIAG);
        }
        break;
    }
}

static void app_task(void *arg)
{
    (void)arg;
    fill_diag_screen();
    for (;;) {
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) == pdTRUE) {
            handle_event(&ev);
        }
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    const app_event_t ev = {.id = APP_EV_TICK};
    app_post(&ev);
}

esp_err_t app_init(void)
{
    s_queue = xQueueCreate(APP_EVENT_QUEUE_DEPTH, sizeof(app_event_t));
    s_snapshot_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_queue != NULL && s_snapshot_lock != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    s_snapshot.state = APP_STATE_BOOT;
    return app_cmds_register();
}

esp_err_t app_start(void)
{
    const app_task_cfg_t cfg = APP_TASK_APP;
    const BaseType_t ok = xTaskCreate(app_task, cfg.name, cfg.stack, NULL, cfg.prio, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");

    const esp_timer_create_args_t tick = {
        .callback = tick_cb,
        .name = "app_tick",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick, &s_tick_timer), TAG, "tick timer");
    return esp_timer_start_periodic(s_tick_timer, TICK_PERIOD_US);
}
