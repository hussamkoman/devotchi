#include "Pairing.hpp"

#include "Config.hpp"

#include "esp_http_client.h"
#include "esp_log.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace devotchi::pairing {

namespace {
constexpr const char* TAG = "Pairing";
constexpr size_t      MAX_RESPONSE = 512;

struct ResponseBuf {
    char   data[MAX_RESPONSE];
    size_t len;
};

esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        auto* buf = static_cast<ResponseBuf*>(evt->user_data);
        if (!buf) return ESP_OK;
        size_t copy_len = evt->data_len;
        if (buf->len + copy_len >= MAX_RESPONSE) {
            copy_len = MAX_RESPONSE - 1 - buf->len;
        }
        if (copy_len > 0) {
            std::memcpy(buf->data + buf->len, evt->data, copy_len);
            buf->len += copy_len;
            buf->data[buf->len] = '\0';
        }
    }
    return ESP_OK;
}
}  // namespace

std::optional<ClaimResult> claim(std::string_view code) {
    char url[128];
    std::snprintf(url, sizeof(url), "http://%s:%u/api/pair/claim",
                  config::network::SERVER_HOST,
                  static_cast<unsigned>(config::network::SERVER_PORT));

    char body[64];
    std::snprintf(body, sizeof(body), "{\"code\":\"%.*s\"}",
                  static_cast<int>(code.size()), code.data());

    ResponseBuf rb{};

    esp_http_client_config_t cfg = {};
    cfg.url            = url;
    cfg.method         = HTTP_METHOD_POST;
    cfg.timeout_ms     = static_cast<int>(config::network::HTTP_TIMEOUT_MS);
    cfg.event_handler  = http_event_handler;
    cfg.user_data      = &rb;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return std::nullopt;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, std::strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "POST failed: %s", esp_err_to_name(err));
        return std::nullopt;
    }
    if (status != 201 && status != 200) {
        ESP_LOGW(TAG, "claim rejected (status=%d, body=%.*s)",
                 status, static_cast<int>(rb.len), rb.data);
        return std::nullopt;
    }

    JsonDocument doc;
    auto perr = deserializeJson(doc, rb.data, rb.len);
    if (perr) {
        ESP_LOGE(TAG, "response JSON parse failed: %s", perr.c_str());
        return std::nullopt;
    }

    ClaimResult result;
    result.token   = doc["token"]  | "";
    result.room_id = doc["roomId"] | "";
    if (result.token.empty() || result.room_id.empty()) {
        ESP_LOGW(TAG, "response missing token or roomId");
        return std::nullopt;
    }

    ESP_LOGI(TAG, "claim succeeded — room=%s", result.room_id.c_str());
    return result;
}

}  // namespace devotchi::pairing
