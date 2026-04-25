#include "Wifi.hpp"

#include "esp_log.h"

namespace devotchi::wifi {

namespace {
constexpr const char* TAG = "Wifi";
}

void Wifi::init(storage::Storage& storage) {
    storage_ = &storage;
    ESP_LOGI(TAG, "Wifi init (stub — wire up esp_wifi + provisioning here)");
}

void Wifi::start() {
    ESP_LOGI(TAG, "Wifi start (stub)");
}

void Wifi::stop() {
    ESP_LOGI(TAG, "Wifi stop (stub)");
}

}  // namespace devotchi::wifi
