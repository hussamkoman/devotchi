#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace devotchi::protocol {

enum class Mood : uint8_t {
    // PetMood values (notify, mood)
    Happy, Sad, Excited, Anxious, Sleeping, Angry, Proud, Idle,
    // StatusMood values (status)
    Focused, Working, Thinking,
};

struct StatusMessage {
    std::string msg;
    Mood mood = Mood::Working;
    std::string timestamp;
};

struct ApprovalMessage {
    std::string id;
    std::string desc;
    std::string details;
    uint32_t timeout_ms = 0;
    std::string timestamp;
};

struct PermissionMessage {
    std::string id;
    std::string action;
    std::string details;
    uint32_t timeout_ms = 0;
    std::string timestamp;
};

struct NotifyMessage {
    std::string event;
    Mood mood = Mood::Idle;
    std::string detail;
    std::string timestamp;
};

struct MoodMessage {
    Mood mood = Mood::Idle;
    std::string reason;
    std::string timestamp;
};

struct DismissMessage {
    std::string id;
};

using Message = std::variant<
    StatusMessage,
    ApprovalMessage,
    PermissionMessage,
    NotifyMessage,
    MoodMessage,
    DismissMessage
>;

// Inbound: parse a JSON payload into a Message variant.
std::optional<Message> parse(std::string_view json);

// Outbound: build JSON payloads for the messages we send.
std::string serialize_register(std::string_view role,
                               std::string_view room_id,
                               std::string_view token);
std::string serialize_approval_response(std::string_view id, bool approved);
std::string serialize_permission_response(std::string_view id, bool granted);
std::string serialize_pong();

}  // namespace devotchi::protocol
