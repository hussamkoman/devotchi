#include "WsClient.hpp"

#include "esp_log.h"

namespace devotchi::ws_client {

namespace {
constexpr const char* TAG = "WsClient";
}

void WsClient::init(storage::Storage& storage) {
    storage_ = &storage;
    ESP_LOGI(TAG, "WsClient init (stub — wire up esp_websocket_client here)");
}

void WsClient::start() { ESP_LOGI(TAG, "WsClient start (stub)"); }
void WsClient::stop()  { ESP_LOGI(TAG, "WsClient stop (stub)"); }
void WsClient::send(std::string_view) {}

}  // namespace devotchi::ws_client
