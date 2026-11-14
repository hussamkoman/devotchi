#include "Protocol.hpp"

#include "esp_log.h"

#include <ArduinoJson.h>

namespace devotchi::protocol {

namespace {
constexpr const char* TAG = "Protocol";

Mood mood_from_str(const char* s) {
    if (!s) return Mood::Idle;
    std::string_view v{s};
    if (v == "happy")     return Mood::Happy;
    if (v == "sad")       return Mood::Sad;
    if (v == "excited")   return Mood::Excited;
    if (v == "anxious")   return Mood::Anxious;
    if (v == "sleeping")  return Mood::Sleeping;
    if (v == "angry")     return Mood::Angry;
    if (v == "proud")     return Mood::Proud;
    if (v == "focused")   return Mood::Focused;
    if (v == "working")   return Mood::Working;
    if (v == "thinking")  return Mood::Thinking;
    return Mood::Idle;
}

const char* mood_to_str(Mood m) {
    switch (m) {
        case Mood::Happy:     return "happy";
        case Mood::Sad:       return "sad";
        case Mood::Excited:   return "excited";
        case Mood::Anxious:   return "anxious";
        case Mood::Sleeping:  return "sleeping";
        case Mood::Angry:     return "angry";
        case Mood::Proud:     return "proud";
        case Mood::Focused:   return "focused";
        case Mood::Working:   return "working";
        case Mood::Thinking:  return "thinking";
        case Mood::Idle:      return "idle";
    }
    return "idle";
}
}  // namespace

std::optional<Message> parse(std::string_view json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json.data(), json.size());
    if (err) {
        ESP_LOGW(TAG, "parse failed: %s", err.c_str());
        return std::nullopt;
    }

    const char* type = doc["type"] | "";
    if (!type || !*type) return std::nullopt;
    std::string_view t{type};

    if (t == "status") {
        StatusMessage m;
        m.msg       = doc["msg"]       | "";
        m.mood      = mood_from_str(doc["mood"]      | "working");
        m.timestamp = doc["timestamp"] | "";
        return m;
    }
    if (t == "approval") {
        ApprovalMessage m;
        m.id         = doc["id"]        | "";
        m.desc       = doc["desc"]      | "";
        m.details    = doc["details"]   | "";
        m.timeout_ms = doc["timeout"]   | 0u;
        m.timestamp  = doc["timestamp"] | "";
        return m;
    }
    if (t == "permission") {
        PermissionMessage m;
        m.id         = doc["id"]        | "";
        m.action     = doc["action"]    | "";
        m.details    = doc["details"]   | "";
        m.timeout_ms = doc["timeout"]   | 0u;
        m.timestamp  = doc["timestamp"] | "";
        return m;
    }
    if (t == "notify") {
        NotifyMessage m;
        m.event     = doc["event"]     | "";
        m.mood      = mood_from_str(doc["mood"] | "idle");
        m.detail    = doc["detail"]    | "";
        m.timestamp = doc["timestamp"] | "";
        return m;
    }
    if (t == "mood") {
        MoodMessage m;
        m.mood      = mood_from_str(doc["mood"] | "idle");
        m.reason    = doc["reason"]    | "";
        m.timestamp = doc["timestamp"] | "";
        return m;
    }
    if (t == "dismiss") {
        DismissMessage m;
        m.id = doc["id"] | "";
        return m;
    }

    return std::nullopt;
}

std::string serialize_register(std::string_view role,
                               std::string_view room_id,
                               std::string_view token) {
    JsonDocument doc;
    doc["type"] = "register";
    doc["role"] = std::string(role);
    doc["roomId"] = std::string(room_id);
    if (!token.empty()) doc["token"] = std::string(token);

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string serialize_approval_response(std::string_view id, bool approved) {
    JsonDocument doc;
    doc["type"] = "approval_response";
    doc["id"] = std::string(id);
    doc["approved"] = approved;

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string serialize_permission_response(std::string_view id, bool granted) {
    JsonDocument doc;
    doc["type"] = "permission_response";
    doc["id"] = std::string(id);
    doc["granted"] = granted;

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string serialize_pong() {
    return R"({"type":"pong"})";
}

}  // namespace devotchi::protocol
