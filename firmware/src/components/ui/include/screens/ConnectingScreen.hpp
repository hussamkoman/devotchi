#pragma once

#include "Screen.hpp"

#include <string>

namespace devotchi::ui {

class ConnectingScreen : public Screen {
public:
    explicit ConnectingScreen(std::string label = "Connecting...");

    void enter() override;
    void on_button(buttons::ButtonEvent ev) override;

private:
    std::string label_;
};

}  // namespace devotchi::ui
