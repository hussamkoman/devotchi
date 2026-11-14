#pragma once

#include "Storage.hpp"

#include <functional>

namespace devotchi::wifi {

enum class State {
    Disconnected,
    Provisioning,
    Connecting,
    Connected,
};

class Wifi {
public:
    using StateCallback = std::function<void(State)>;

    void init(storage::Storage& storage);
    void start();
    void stop();

    State state() const { return state_; }
    void  on_state_change(StateCallback cb) { cb_ = std::move(cb); }

    // Called from ESP-IDF event handlers — public so the C-style callbacks can reach in.
    void internal_set_state(State s);
    void internal_save_creds(const char* ssid, const char* psk);

private:
    void start_provisioning();

    storage::Storage* storage_ = nullptr;
    State             state_   = State::Disconnected;
    StateCallback     cb_;
};

}  // namespace devotchi::wifi
