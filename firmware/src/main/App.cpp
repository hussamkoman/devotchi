#include "App.hpp"

#include "Pairing.hpp"
#include "screens/ApprovalScreen.hpp"
#include "screens/ConnectingScreen.hpp"
#include "screens/PairingScreen.hpp"
#include "screens/PetScreen.hpp"
#include "screens/ProvisioningScreen.hpp"
#include "screens/SettingsScreen.hpp"
#include "screens/SplashScreen.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h / queue.h
#include "freertos/queue.h"
#include "freertos/task.h"

#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>

namespace devotchi {

namespace {
constexpr const char* TAG = "App";
constexpr UBaseType_t EVENT_QUEUE_DEPTH = 16;

uint32_t now_ms() { return esp_timer_get_time() / 1000ULL; }
}  // namespace

void App::run() {
    ESP_LOGI(TAG, "Devotchi booting");

    storage_.init();
    display_.init();
    ui_.init(display_);
    buttons_.init();
    feedback_.init();
    power_.init(display_);
    wifi_.init(storage_);
    ws_.init(storage_);

    event_q_ = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(AppEvent));
    configASSERT(event_q_ != nullptr);

    wifi_.on_state_change([this](wifi::State s) { on_wifi_state(s); });
    ws_.on_state_change  ([this](ws_client::State s) { on_ws_state(s); });
    ws_.on_message       ([this](const protocol::Message& m) { on_ws_message(m); });

    ui_task_.spawn();
    pwr_task_.spawn();

    // Splash for ~1 second, then start the WiFi flow.
    ui_.show(std::make_unique<ui::SplashScreen>());
    ui_.set_status_bar_visible(false);
    vTaskDelay(pdMS_TO_TICKS(1000));

    wifi_.start();   // either connects with stored creds or starts BLE provisioning

    // Non-blocking stdin so we can poll for serial commands in the main loop.
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    std::string console_buf;

    last_input_ms_ = now_ms();
    ESP_LOGI(TAG, "Devotchi ready");
    ESP_LOGI(TAG, "Serial commands: 'pair <CODE>'  'reset'");

    while (true) {
        // Drain serial input — supports `pair <CODE>` and `reset` commands.
        char cbuf[64];
        const ssize_t cn = read(STDIN_FILENO, cbuf, sizeof(cbuf));
        if (cn > 0) {
            for (ssize_t i = 0; i < cn; ++i) {
                const char c = cbuf[i];
                if (c == '\n' || c == '\r') {
                    if (!console_buf.empty()) {
                        if (console_buf.rfind("pair ", 0) == 0) {
                            std::string code = console_buf.substr(5);
                            while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) code.pop_back();
                            for (auto& ch : code) ch = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
                            ESP_LOGI(TAG, "console: pair '%s'", code.c_str());
                            try_claim_pairing(code);
                        } else if (console_buf == "reset") {
                            ESP_LOGI(TAG, "console: reset");
                            factory_reset();
                        } else {
                            ESP_LOGW(TAG, "console: unknown command '%s'", console_buf.c_str());
                        }
                        console_buf.clear();
                    }
                } else if (console_buf.size() < 32) {
                    console_buf += c;
                }
            }
        }

        // Buttons → UI + feedback + sleep wake.
        buttons::ButtonEvent ev;
        while (buttons_.poll(ev, 0)) {
            last_input_ms_ = now_ms();
            if (dimmed_) {
                power_.backlight(brightness_pct_);
                dimmed_ = false;
            }

            ui_.on_button(ev);

            if (ev.kind == buttons::EventKind::Press) {
                switch (ev.id) {
                    case buttons::ButtonId::Approve: feedback_.flash(80); break;
                    case buttons::ButtonId::Reject:  feedback_.buzz(80);  break;
                    case buttons::ButtonId::Action:  feedback_.flash(80); break;
                }
            } else if (ev.kind == buttons::EventKind::LongPress &&
                       ev.id == buttons::ButtonId::Action &&
                       state_ == AppState::Idle) {
                post_event(AppEvent::EnterSettings);
            }
        }

        // App events.
        AppEvent app_ev;
        while (xQueueReceive(static_cast<QueueHandle_t>(event_q_), &app_ev, 0) == pdTRUE) {
            switch (app_ev) {
                case AppEvent::WifiProvisioning:
                    state_ = AppState::Provisioning;
                    show_provisioning();
                    break;
                case AppEvent::WifiConnected:
                    if (storage_.token() && storage_.room_id()) {
                        state_ = AppState::Connecting;
                        show_connecting("Connecting to server...");
                        ws_.start();
                    } else {
                        state_ = AppState::NeedsPairing;
                        show_pairing();
                    }
                    break;
                case AppEvent::WifiDisconnected:
                    // Let esp_wifi auto-reconnect; UI just shows status.
                    break;
                case AppEvent::WsRegistered:
                    state_ = AppState::Idle;
                    show_pet();
                    break;
                case AppEvent::WsDisconnected:
                    if (state_ == AppState::Idle) {
                        // Stay on PetScreen; status bar will dim the paired dot.
                    }
                    break;
                case AppEvent::PairingClaimed:
                    state_ = AppState::Connecting;
                    show_connecting("Connecting to server...");
                    ws_.start();
                    break;
                case AppEvent::PairingFailed:
                    // Handled inline in try_claim_pairing — screen stays on PairingScreen.
                    break;
                case AppEvent::BackToIdle:
                    state_ = AppState::Idle;
                    show_pet();
                    break;
                case AppEvent::EnterSettings:
                    state_ = AppState::Settings;
                    show_settings();
                    break;
            }
        }

        // Periodic status bar update.
        const auto batt = power_.latest();
        ui_.set_status(batt.pct, batt.charging,
                       wifi_connected_.load(),
                       ws_registered_.load());

        // Backlight dim on idle.
        const uint32_t idle_for = now_ms() - last_input_ms_;
        if (!dimmed_ && idle_for > sleep_ms_) {
            power_.backlight(10);
            dimmed_ = true;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void App::on_wifi_state(wifi::State s) {
    switch (s) {
        case wifi::State::Provisioning:
            post_event(AppEvent::WifiProvisioning);
            break;
        case wifi::State::Connected:
            wifi_connected_.store(true);
            post_event(AppEvent::WifiConnected);
            break;
        case wifi::State::Disconnected:
            wifi_connected_.store(false);
            post_event(AppEvent::WifiDisconnected);
            break;
        case wifi::State::Connecting:
            wifi_connected_.store(false);
            break;
    }
}

void App::on_ws_state(ws_client::State s) {
    switch (s) {
        case ws_client::State::Registered:
            ws_registered_.store(true);
            post_event(AppEvent::WsRegistered);
            break;
        case ws_client::State::Disconnected:
            ws_registered_.store(false);
            post_event(AppEvent::WsDisconnected);
            break;
        default:
            break;
    }
}

void App::on_ws_message(const protocol::Message& msg) {
    if (auto* a = std::get_if<protocol::ApprovalMessage>(&msg)) {
        auto screen = std::make_unique<ui::ApprovalScreen>(
            a->id, ui::ApprovalScreen::Kind::Approval,
            "Approval requested", a->desc, a->timeout_ms,
            [this](const std::string& id, ui::ApprovalScreen::Kind, bool approved) {
                ws_.send(protocol::serialize_approval_response(id, approved));
                post_event(AppEvent::BackToIdle);
            });
        state_ = AppState::Approval;
        ui_.show(std::move(screen));
        ui_.set_status_bar_visible(true);
        return;
    }
    if (auto* p = std::get_if<protocol::PermissionMessage>(&msg)) {
        auto screen = std::make_unique<ui::ApprovalScreen>(
            p->id, ui::ApprovalScreen::Kind::Permission,
            "Permission requested", p->action + "\n" + p->details, p->timeout_ms,
            [this](const std::string& id, ui::ApprovalScreen::Kind, bool granted) {
                ws_.send(protocol::serialize_permission_response(id, granted));
                post_event(AppEvent::BackToIdle);
            });
        state_ = AppState::Approval;
        ui_.show(std::move(screen));
        ui_.set_status_bar_visible(true);
        return;
    }
    // Mood / Status / Notify / Dismiss flow into the active screen for in-place updates.
    ui_.on_message(msg);
}

void App::post_event(AppEvent ev) {
    if (!event_q_) return;
    xQueueSend(static_cast<QueueHandle_t>(event_q_), &ev, 0);
}

void App::show_pet() {
    ui_.show(std::make_unique<ui::PetScreen>());
    ui_.set_status_bar_visible(true);
}

void App::show_pairing() {
    auto screen = std::make_unique<ui::PairingScreen>(
        [this](const std::string& code) { try_claim_pairing(code); });
    ui_.show(std::move(screen));
    ui_.set_status_bar_visible(false);
}

void App::show_provisioning() {
    ui_.show(std::make_unique<ui::ProvisioningScreen>());
    ui_.set_status_bar_visible(false);
}

void App::show_connecting(const std::string& label) {
    ui_.show(std::make_unique<ui::ConnectingScreen>(label));
    ui_.set_status_bar_visible(true);
}

void App::show_settings() {
    auto screen = std::make_unique<ui::SettingsScreen>(
        brightness_pct_,
        sleep_ms_,
        [this](uint8_t pct)   { brightness_pct_ = pct; power_.backlight(pct); },
        [this](uint32_t ms)   { sleep_ms_ = ms; },
        [this]                { factory_reset(); },
        [this]                { post_event(AppEvent::BackToIdle); });
    ui_.show(std::move(screen));
    ui_.set_status_bar_visible(true);
}

void App::try_claim_pairing(const std::string& code) {
    ESP_LOGI(TAG, "claiming pairing code %s", code.c_str());
    auto result = pairing::claim(code);
    if (!result) {
        // Stay on PairingScreen; the screen will keep accepting input.
        ESP_LOGW(TAG, "pairing claim failed");
        // (Best-effort: PairingScreen exposes show_error; we'd need a handle to call it.
        //  Skipped here — user can retry by re-entering the code.)
        post_event(AppEvent::PairingFailed);
        return;
    }
    storage_.set_token_and_room(result->token, result->room_id);
    storage_.set_pairing_code(code);
    post_event(AppEvent::PairingClaimed);
}

void App::factory_reset() {
    ESP_LOGW(TAG, "factory reset — clearing NVS and restarting");
    storage_.clear();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

}  // namespace devotchi
