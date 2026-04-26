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
    LongPress,
};

struct ButtonEvent {
    ButtonId  id;
    EventKind kind;
};

/** Surfaces 3 hardware buttons as queued ButtonEvents. Drained via poll(). */
class Buttons {
public:
    void init();

    // Non-blocking when timeout_ms == 0. Returns false if no event available.
    bool poll(ButtonEvent& out, uint32_t timeout_ms = 0);

private:
    struct ButtonContext {
        Buttons*  self;
        ButtonId  id;
    };

    void* approve_handle_ = nullptr;   // button_handle_t — opaque in header
    void* reject_handle_  = nullptr;
    void* action_handle_  = nullptr;

    ButtonContext approve_ctx_{this, ButtonId::Approve};
    ButtonContext reject_ctx_ {this, ButtonId::Reject};
    ButtonContext action_ctx_ {this, ButtonId::Action};

    void* queue_ = nullptr;            // QueueHandle_t — opaque in header

    static void on_press_cb(void* btn_handle, void* user_data);
    static void on_long_press_cb(void* btn_handle, void* user_data);
};

}  // namespace devotchi::buttons
