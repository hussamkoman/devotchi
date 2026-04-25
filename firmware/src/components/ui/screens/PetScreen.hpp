#pragma once

#include "Protocol.hpp"
#include "Screen.hpp"

namespace devotchi::ui {

class PetScreen : public Screen {
public:
    void enter(display::Display& d) override;
    void tick(display::Display& d) override;
    void on_button(buttons::ButtonEvent ev) override;
    void on_message(const protocol::Message& msg) override;

private:
    protocol::Mood mood_ = protocol::Mood::Idle;
};

}  // namespace devotchi::ui
