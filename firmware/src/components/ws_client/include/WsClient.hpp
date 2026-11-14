#pragma once

#include "Protocol.hpp"
#include "Storage.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace devotchi::ws_client {

enum class State {
    Disconnected,
    Connecting,
    Connected,
    Registered,
};

class WsClient {
public:
    using MessageCallback = std::function<void(const protocol::Message&)>;
    using StateCallback   = std::function<void(State)>;

    void init(storage::Storage& storage);
    void start();
    void stop();

    void send(std::string_view json);

    State state() const { return state_; }
    void  on_message(MessageCallback cb)        { msg_cb_   = std::move(cb); }
    void  on_state_change(StateCallback cb)     { state_cb_ = std::move(cb); }

    // Called from esp_websocket_client event callback — public so the C-style
    // trampoline can reach in.
    void internal_handle_event(int32_t event_id, const void* event_data);

private:
    void set_state(State s);

    storage::Storage* storage_  = nullptr;
    void*             handle_   = nullptr;   // esp_websocket_client_handle_t
    std::string       room_id_;
    State             state_    = State::Disconnected;
    MessageCallback   msg_cb_;
    StateCallback     state_cb_;
};

}  // namespace devotchi::ws_client
