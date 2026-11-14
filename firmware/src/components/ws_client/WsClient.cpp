#include "WsClient.hpp"

#include "Config.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

#include <cstdio>
#include <cstring>

namespace devotchi::ws_client {

namespace {
constexpr const char* TAG = "WsClient";

void event_trampoline(void* handler_arg, esp_event_base_t /*base*/,
                      int32_t event_id, void* event_data) {
    auto* self = static_cast<WsClient*>(handler_arg);
    if (self) self->internal_handle_event(event_id, event_data);
}
}  // namespace

void WsClient::init(storage::Storage& storage) {
    storage_ = &storage;
    ESP_LOGI(TAG, "init");
}

void WsClient::start() {
    auto token = storage_->token();
    auto room  = storage_->room_id();
    if (!token || !room) {
        ESP_LOGW(TAG, "start — missing token or room_id, not connecting");
        return;
    }

    room_id_ = *room;

    char url[256];
    std::snprintf(url, sizeof(url), "ws://%s:%u/?token=%s",
                  config::network::SERVER_HOST,
                  static_cast<unsigned>(config::network::SERVER_PORT),
                  token->c_str());

    esp_websocket_client_config_t cfg = {};
    cfg.uri                   = url;
    cfg.reconnect_timeout_ms  = static_cast<int>(config::network::WS_RECONNECT_DELAY_MS);
    cfg.network_timeout_ms    = static_cast<int>(config::network::HTTP_TIMEOUT_MS);

    auto h = esp_websocket_client_init(&cfg);
    if (!h) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        return;
    }
    handle_ = h;

    esp_websocket_register_events(h, WEBSOCKET_EVENT_ANY, &event_trampoline, this);
    esp_websocket_client_start(h);

    set_state(State::Connecting);
    ESP_LOGI(TAG, "start — %s", url);
}

void WsClient::stop() {
    if (!handle_) return;
    auto h = static_cast<esp_websocket_client_handle_t>(handle_);
    esp_websocket_client_stop(h);
    esp_websocket_client_destroy(h);
    handle_ = nullptr;
    set_state(State::Disconnected);
}

void WsClient::send(std::string_view json) {
    if (!handle_) return;
    auto h = static_cast<esp_websocket_client_handle_t>(handle_);
    esp_websocket_client_send_text(h, json.data(), json.size(), portMAX_DELAY);
}

void WsClient::set_state(State s) {
    state_ = s;
    if (state_cb_) state_cb_(s);
}

void WsClient::internal_handle_event(int32_t event_id, const void* event_data) {
    auto* data = static_cast<const esp_websocket_event_data_t*>(event_data);

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "connected — sending register");
            set_state(State::Connected);
            auto reg = protocol::serialize_register("consumer", room_id_, "");
            send(reg);
            break;
        }
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "disconnected");
            set_state(State::Disconnected);
            break;
        case WEBSOCKET_EVENT_DATA: {
            if (!data || data->op_code != 0x1 /*text frame*/) break;
            std::string_view payload(data->data_ptr, data->data_len);

            // Inspect type field cheaply for system messages before full parse.
            if (payload.find("\"register_ack\"") != std::string_view::npos) {
                ESP_LOGI(TAG, "registered with server");
                set_state(State::Registered);
                break;
            }
            if (payload.find("\"ping\"") != std::string_view::npos) {
                send(protocol::serialize_pong());
                break;
            }

            auto msg = protocol::parse(payload);
            if (msg && msg_cb_) msg_cb_(*msg);
            break;
        }
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGW(TAG, "error event");
            break;
        default:
            break;
    }
}

}  // namespace devotchi::ws_client
