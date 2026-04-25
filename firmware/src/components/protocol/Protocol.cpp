#include "Protocol.hpp"

#include "esp_log.h"

namespace devotchi::protocol {

namespace {
constexpr const char* TAG = "Protocol";
}

std::optional<Message> parse(std::string_view) {
    ESP_LOGW(TAG, "parse() not implemented yet");
    return std::nullopt;
}

std::string serialize_register(std::string_view, std::string_view, std::string_view) {
    return {};
}

std::string serialize_approval_response(std::string_view, bool) {
    return {};
}

std::string serialize_permission_response(std::string_view, bool) {
    return {};
}

std::string serialize_pong() {
    return R"({"type":"pong"})";
}

}  // namespace devotchi::protocol
