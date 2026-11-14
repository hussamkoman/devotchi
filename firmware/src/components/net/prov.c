#include "prov.h"

#include "pair_code.h"
#include "storage.h"
#include "url.h"

#include "esp_bt.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "prov";

#define ENDPOINT_SERVER   "custom-data"
#define SERVER_PREFIX     "server="
#define LOCK_TIMEOUT_MS   100
/* Largest multiple of the alphabet size below 256: bytes above it would bias the draw. */
#define POP_REJECT_FROM   (256 - 256 % PAIR_CODE_ALPHABET_LEN)

static net_event_cb_t s_cb;
static SemaphoreHandle_t s_lock;

/* Guarded by s_lock: written by the system event loop, read by the app task. */
static bool s_has_credentials;
static char s_ssid[PROV_SSID_SIZE];
static char s_psk[PROV_PSK_SIZE];

static void post(net_event_id_t id, net_wifi_fail_t fail)
{
    const net_event_t ev = {.id = id, .u.wifi_fail = fail};
    s_cb(&ev);
}

/* Runs in the system event loop. */
static void on_prov_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case NETWORK_PROV_WIFI_CRED_RECV: {
        const wifi_sta_config_t *cfg = data;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE) {
            memset(s_ssid, 0, sizeof(s_ssid));
            memset(s_psk, 0, sizeof(s_psk));
            memcpy(s_ssid, cfg->ssid, sizeof(cfg->ssid));
            memcpy(s_psk, cfg->password, sizeof(cfg->password));
            s_has_credentials = true;
            xSemaphoreGive(s_lock);
        }
        post(NET_EV_PROV_CRED_RECV, NET_WIFI_FAIL_OTHER);
        break;
    }
    case NETWORK_PROV_WIFI_CRED_FAIL: {
        const network_prov_wifi_sta_fail_reason_t *reason = data;
        post(NET_EV_PROV_CRED_FAIL, *reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR ? NET_WIFI_FAIL_WRONG_PASSWORD
                                                                                : NET_WIFI_FAIL_NOT_FOUND);
        break;
    }
    case NETWORK_PROV_WIFI_CRED_SUCCESS:
        post(NET_EV_PROV_CRED_SUCCESS, NET_WIFI_FAIL_OTHER);
        break;
    case NETWORK_PROV_END:
        post(NET_EV_PROV_END, NET_WIFI_FAIL_OTHER);
        break;
    default:
        break;
    }
}

static esp_err_t reply(const char *text, uint8_t **outbuf, ssize_t *outlen)
{
    /* protocomm frees the reply. */
    *outbuf = (uint8_t *)strdup(text);
    if (*outbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *outlen = (ssize_t)strlen(text) + 1;
    return ESP_OK;
}

/* Runs in the Bluetooth host task. Payload: "server=<base url>". */
static esp_err_t on_server_endpoint(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf,
                                    ssize_t *outlen, void *priv_data)
{
    (void)session_id;
    (void)priv_data;
    char text[sizeof(SERVER_PREFIX) + URL_MAX_LEN + 2];
    if (inbuf == NULL || inlen <= 0 || (size_t)inlen >= sizeof(text)) {
        return reply("ERR bad url", outbuf, outlen);
    }
    memcpy(text, inbuf, (size_t)inlen);
    text[inlen] = '\0';
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) {
        text[--len] = '\0';
    }

    url_t url;
    char base[URL_BASE_SIZE];
    if (strncmp(text, SERVER_PREFIX, strlen(SERVER_PREFIX)) != 0 || !url_parse(text + strlen(SERVER_PREFIX), &url) ||
        url_base(&url, base, sizeof(base)) < 0) {
        return reply("ERR bad url", outbuf, outlen);
    }
    if (storage_set_server_url(base) != ESP_OK) {
        return reply("ERR storage", outbuf, outlen);
    }
    ESP_LOGI(TAG, "server url set to %s", base);
    return reply("OK", outbuf, outlen);
}

static void make_pop(char pop[PROV_POP_SIZE])
{
    size_t n = 0;
    while (n < PROV_POP_LEN) {
        uint8_t bytes[4];
        esp_fill_random(bytes, sizeof(bytes));
        for (size_t i = 0; i < sizeof(bytes) && n < PROV_POP_LEN; i++) {
            if (bytes[i] < POP_REJECT_FROM) {
                pop[n++] = PAIR_CODE_ALPHABET[bytes[i] % PAIR_CODE_ALPHABET_LEN];
            }
        }
    }
    pop[PROV_POP_LEN] = '\0';
}

esp_err_t prov_start(net_event_cb_t cb, prov_info_t *out)
{
    s_cb = cb;
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex");
    }

    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    const int n = snprintf(out->service_name, sizeof(out->service_name), "%s-%02X%02X",
                           CONFIG_DEVOTCHI_PROV_NAME_PREFIX, mac[4], mac[5]);
    ESP_RETURN_ON_FALSE(n > 0 && (size_t)n < sizeof(out->service_name), ESP_ERR_INVALID_SIZE, TAG, "service name");
    make_pop(out->pop);

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, on_prov_event, NULL,
                                                            NULL),
                        TAG, "handler");
    const network_prov_mgr_config_t cfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(cfg), TAG, "manager init");

    esp_err_t err = network_prov_mgr_endpoint_create(ENDPOINT_SERVER);
    if (err == ESP_OK) {
        err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, out->pop, out->service_name, NULL);
    }
    if (err == ESP_OK) {
        err = network_prov_mgr_endpoint_register(ENDPOINT_SERVER, on_server_endpoint, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        network_prov_mgr_deinit();
        return err;
    }
    ESP_LOGI(TAG, "provisioning as %s", out->service_name);
    return ESP_OK;
}

bool prov_get_credentials(char ssid[PROV_SSID_SIZE], char psk[PROV_PSK_SIZE])
{
    bool ok = false;
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE) {
        if (s_has_credentials) {
            memcpy(ssid, s_ssid, sizeof(s_ssid));
            memcpy(psk, s_psk, sizeof(s_psk));
            ok = true;
        }
        xSemaphoreGive(s_lock);
    }
    return ok;
}

void prov_retry_after_failure(void)
{
    const esp_err_t err = network_prov_mgr_reset_wifi_sm_state_on_failure();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reset after failure: %s", esp_err_to_name(err));
    }
}

void prov_finish(void)
{
    network_prov_mgr_deinit();
}

void prov_release_bt_memory(void)
{
    const esp_err_t err = esp_bt_mem_release(ESP_BT_MODE_BTDM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bt memory release: %s", esp_err_to_name(err));
    }
}
