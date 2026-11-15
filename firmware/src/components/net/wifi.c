#include "wifi.h"

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi";

static net_event_cb_t s_cb;
static esp_netif_t *s_netif;
static atomic_bool s_ready;
static atomic_bool s_started;
static atomic_bool s_connected;

#define FAST_MAGIC 0x57494649u

/*
 * The access point of the last connection, kept in RTC memory so a wake from
 * deep sleep can skip the scan. The SSID guards against credentials that
 * changed in between.
 */
typedef struct {
    uint32_t magic;
    uint8_t bssid[6];
    uint8_t channel;
    char ssid[WIFI_SSID_SIZE];
} fast_hint_t;

static RTC_DATA_ATTR fast_hint_t s_hint;
/* The hint is tried once per boot, on the first attempt after a deep-sleep wake. */
static bool s_hint_unused;

static net_wifi_fail_t classify(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return NET_WIFI_FAIL_WRONG_PASSWORD;
    case WIFI_REASON_ASSOC_LEAVE:
        return NET_WIFI_FAIL_LEFT;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return NET_WIFI_FAIL_NOT_FOUND;
    default:
        return NET_WIFI_FAIL_OTHER;
    }
}

/* Runs in the system event loop: translate and post, nothing else. */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        atomic_store(&s_connected, false);
        ESP_LOGW(TAG, "disconnected, reason %u", d->reason);
        const net_event_t ev = {.id = NET_EV_WIFI_DISCONNECTED, .u.wifi_fail = classify(d->reason)};
        s_cb(&ev);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;
        atomic_store(&s_connected, true);
        wifi_ap_record_t ap;
        wifi_config_t cfg;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
            memcpy(s_hint.bssid, ap.bssid, sizeof(s_hint.bssid));
            s_hint.channel = ap.primary;
            memset(s_hint.ssid, 0, sizeof(s_hint.ssid));
            memcpy(s_hint.ssid, cfg.sta.ssid, sizeof(s_hint.ssid) - 1);
            s_hint.magic = FAST_MAGIC;
        }
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&got->ip_info.ip));
        const net_event_t ev = {.id = NET_EV_WIFI_GOT_IP};
        s_cb(&ev);
    }
}

esp_err_t wifi_init(net_event_cb_t cb, bool use_nvs)
{
    s_cb = cb;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_ERR_NO_MEM, TAG, "sta netif");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (!use_nvs) {
        cfg.nvs_enable = 0;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL,
                                                            NULL),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL), TAG,
                        "ip handler");
    s_hint_unused = esp_reset_reason() == ESP_RST_DEEPSLEEP && s_hint.magic == FAST_MAGIC;
    atomic_store(&s_ready, true);
    return ESP_OK;
}

esp_err_t wifi_connect(const char *ssid, const char *psk)
{
    ESP_RETURN_ON_FALSE(atomic_load(&s_ready), ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0' && psk != NULL, ESP_ERR_INVALID_ARG, TAG, "credentials");

    wifi_config_t cfg = {0};
    ESP_RETURN_ON_FALSE(strlen(ssid) <= sizeof(cfg.sta.ssid) && strlen(psk) < sizeof(cfg.sta.password),
                        ESP_ERR_INVALID_ARG, TAG, "credentials too long");
    /* A 32-byte SSID fills the field without a terminator, which the driver accepts. */
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(cfg.sta.password, psk, strlen(psk));
    if (s_hint_unused) {
        s_hint_unused = false;
        if (strncmp(s_hint.ssid, ssid, sizeof(s_hint.ssid)) == 0) {
            /* A failure falls back to a full scan on the next attempt, which no longer has the hint. */
            cfg.sta.channel = s_hint.channel;
            cfg.sta.bssid_set = true;
            memcpy(cfg.sta.bssid, s_hint.bssid, sizeof(cfg.sta.bssid));
            ESP_LOGI(TAG, "fast reconnect on channel %u", s_hint.channel);
        }
    }

    if (!atomic_load(&s_started)) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
        atomic_store(&s_started, true);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    } else {
        /* Ends a connection or an attempt in progress; the event it causes is classified as NET_WIFI_FAIL_LEFT. */
        esp_wifi_disconnect();
    }
    /*
     * The credentials live in the devotchi namespace; the driver's own copy is
     * left as it is. Set on every call because the provisioning manager
     * switches the driver back to flash storage when it ends.
     */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "config");
    ESP_LOGI(TAG, "connecting to %s", ssid);
    return esp_wifi_connect();
}

void wifi_get_status(wifi_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!atomic_load(&s_ready) || !atomic_load(&s_connected)) {
        return;
    }
    out->connected = true;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(out->ssid, ap.ssid, sizeof(out->ssid) - 1);
        out->rssi = ap.rssi;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) {
        snprintf(out->ip, sizeof(out->ip), IPSTR, IP2STR(&ip.ip));
    }
}

esp_err_t wifi_restore(void)
{
    if (!atomic_load(&s_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_wifi_restore();
}

esp_err_t wifi_set_power_save(bool max)
{
    if (!atomic_load(&s_started)) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_wifi_set_ps(max ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

void wifi_stop(void)
{
    if (atomic_exchange(&s_started, false)) {
        atomic_store(&s_connected, false);
        esp_wifi_stop();
    }
}
