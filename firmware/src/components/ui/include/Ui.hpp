#pragma once

#include "Buttons.hpp"
#include "Display.hpp"
#include "Protocol.hpp"
#include "Screen.hpp"

#include <memory>

namespace devotchi::ui {

class Ui {
public:
    void init(display::Display& display);

    void show(std::unique_ptr<Screen> screen);
    void tick();

    void on_button(buttons::ButtonEvent ev);
    void on_message(const protocol::Message& msg);

private:
    display::Display*        display_ = nullptr;
    std::unique_ptr<Screen>  current_;
};

}  // namespace devotchi::ui
