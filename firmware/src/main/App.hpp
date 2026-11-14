#pragma once

#include "Buttons.hpp"
#include "Display.hpp"
#include "Feedback.hpp"
#include "Power.hpp"
#include "Storage.hpp"
#include "Ui.hpp"
#include "Wifi.hpp"
#include "WsClient.hpp"

#include <atomic>
#include <string>

namespace devotchi {

enum class AppState {
    Boot,
    Provisioning,
    Connecting,
    NeedsPairing,
    Idle,
    Approval,
    Settings,
};

enum class AppEvent : uint8_t {
    WifiConnected,
    WifiProvisioning,
    WifiDisconnected,
    WsRegistered,
    WsDisconnected,
    PairingClaimed,
    PairingFailed,
    BackToIdle,
    EnterSettings,
};

class App {
public:
    void run();

private:
    void on_wifi_state(wifi::State s);
    void on_ws_state(ws_client::State s);
    void on_ws_message(const protocol::Message& msg);
    void post_event(AppEvent ev);

    void show_pet();
    void show_pairing();
    void show_provisioning();
    void show_connecting(const std::string& label);
    void show_settings();
    void try_claim_pairing(const std::string& code);
    void factory_reset();

    storage::Storage    storage_;
    display::Display    display_;
    ui::Ui              ui_;
    ui::UiTask          ui_task_{ui_};
    buttons::Buttons    buttons_;
    feedback::Feedback  feedback_;
    power::Power        power_;
    power::PowerTask    pwr_task_{power_};
    wifi::Wifi          wifi_;
    ws_client::WsClient ws_;

    void* event_q_ = nullptr;   // QueueHandle_t

    AppState state_ = AppState::Boot;

    std::atomic<bool>       wifi_connected_{false};
    std::atomic<bool>       ws_registered_{false};

    uint8_t  brightness_pct_ = 80;
    uint32_t sleep_ms_       = 30'000;
    uint32_t last_input_ms_  = 0;
    bool     dimmed_         = false;
};

}  // namespace devotchi
