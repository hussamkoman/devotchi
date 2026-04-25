#pragma once

#include <cstdint>

namespace devotchi::buttons {

enum class ButtonId : uint8_t {
    Approve,
    Reject,
    Action,
};

enum class EventKind : uint8_t {
    Press,
    Release,
    LongPress,
};

struct ButtonEvent {
    ButtonId  id;
    EventKind kind;
};

class Buttons {
public:
    void init();

    // Non-blocking when timeout_ms == 0. Returns false if no event available.
    bool poll(ButtonEvent& out, uint32_t timeout_ms = 0);
};

}  // namespace devotchi::buttons
