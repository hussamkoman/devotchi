#include "Wifi.hpp"

#include "Config.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "nvs_flash.h"

#include <cstring>

namespace devotchi::wifi {

namespace {
constexpr const char* TAG = "Wifi";

Wifi* g_self = nullptr;

void wifi_event_handler(void* /*arg*/, esp_event_base_t base, int32_t id, void* /*data*/) {
    if (!g_self) return;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "STA disconnected — reconnecting");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "got IP — wifi connected");
        g_self->internal_set_state(State::Connected);
    }
}

void prov_event_handler(void* /*arg*/, esp_event_base_t base, int32_t id, void* data) {
    if (!g_self || base != NETWORK_PROV_EVENT) return;

    switch (id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "provisioning started");
            g_self->internal_set_state(State::Provisioning);
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            auto* creds = static_cast<wifi_sta_config_t*>(data);
            ESP_LOGI(TAG, "received creds for SSID=%s", reinterpret_cast<const char*>(creds->ssid));
            g_self->internal_save_creds(reinterpret_cast<const char*>(creds->ssid),
                                        reinterpret_cast<const char*>(creds->password));
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGE(TAG, "provisioning credential check failed");
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "provisioning credentials accepted");
            break;
        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "provisioning ended — releasing manager");
            network_prov_mgr_deinit();
            break;
        default:
            break;
    }
}
}  // namespace

void Wifi::init(storage::Storage& storage) {
    storage_ = &storage;
    g_self = this;

    ESP_ERROR_CHECK(esp_netif_init());
    if (esp_event_loop_create_default() == ESP_ERR_INVALID_STATE) {
        // Already created — fine.
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,         ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,           IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, nullptr));

    ESP_LOGI(TAG, "init — wifi STA + BLE provisioning ready");
}

void Wifi::start() {
    auto creds = storage_->wifi_creds();
    if (creds) {
        ESP_LOGI(TAG, "start — connecting to saved SSID=%s", creds->ssid.c_str());
        internal_set_state(State::Connecting);

        wifi_config_t wcfg = {};
        std::strncpy(reinterpret_cast<char*>(wcfg.sta.ssid),     creds->ssid.c_str(), sizeof(wcfg.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char*>(wcfg.sta.password), creds->psk.c_str(),  sizeof(wcfg.sta.password) - 1);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        return;
    }

    ESP_LOGI(TAG, "start — no saved creds, beginning BLE provisioning");
    start_provisioning();
}

void Wifi::start_provisioning() {
    network_prov_mgr_config_t pcfg = {};
    pcfg.scheme               = network_prov_scheme_ble;
    pcfg.scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
    pcfg.app_event_handler    = NETWORK_PROV_EVENT_HANDLER_NONE;
    ESP_ERROR_CHECK(network_prov_mgr_init(pcfg));

    ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(
        NETWORK_PROV_SECURITY_1,
        config::network::PROV_POP,
        config::network::PROV_DEVICE_NAME,
        nullptr));

    internal_set_state(State::Provisioning);
}

void Wifi::stop() {
    esp_wifi_stop();
    internal_set_state(State::Disconnected);
}

void Wifi::internal_set_state(State s) {
    state_ = s;
    if (cb_) cb_(s);
}

void Wifi::internal_save_creds(const char* ssid, const char* psk) {
    if (!storage_) return;
    storage::WifiCredentials creds{ssid ? ssid : "", psk ? psk : ""};
    storage_->set_wifi_creds(creds);
}

}  // namespace devotchi::wifi
