#pragma once

#include "Buttons.hpp"
#include "Protocol.hpp"

namespace devotchi::display { class Display; }  // forward decl — passed by ref

namespace devotchi::ui {

class Screen {
public:
    virtual ~Screen() = default;

    virtual void enter(display::Display& d)              = 0;
    virtual void exit()                                   {}
    virtual void tick(display::Display& d)               = 0;
    virtual void on_button(buttons::ButtonEvent ev)      = 0;
    virtual void on_message(const protocol::Message&)    {}
};

}  // namespace devotchi::ui
