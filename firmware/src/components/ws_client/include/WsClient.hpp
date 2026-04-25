#pragma once

#include "Protocol.hpp"
#include "Storage.hpp"

#include <functional>
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

private:
    storage::Storage* storage_  = nullptr;
    State             state_    = State::Disconnected;
    MessageCallback   msg_cb_;
    StateCallback     state_cb_;
};

}  // namespace devotchi::ws_client
