#pragma once

#include "Buttons.hpp"
#include "Display.hpp"
#include "Feedback.hpp"
#include "Power.hpp"
#include "Storage.hpp"
#include "Ui.hpp"
#include "Wifi.hpp"
#include "WsClient.hpp"

namespace devotchi {

class App {
public:
    void run();

private:
    storage::Storage   storage_;
    display::Display   display_;
    ui::Ui             ui_;
    buttons::Buttons   buttons_;
    feedback::Feedback feedback_;
    power::Power       power_;
    wifi::Wifi         wifi_;
    ws_client::WsClient ws_;
};

}  // namespace devotchi
