#pragma once

#include "Screen.hpp"

namespace devotchi::ui {

class ProvisioningScreen : public Screen {
public:
    void enter() override;
    void on_button(buttons::ButtonEvent ev) override;
};

}  // namespace devotchi::ui
