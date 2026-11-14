#pragma once

#include "Buttons.hpp"
#include "Protocol.hpp"

#include "lvgl.h"

namespace devotchi::ui {

/** A single LVGL screen — owns its widget tree and disposes it on destruction. */
class Screen {
public:
    virtual ~Screen() {
        if (root_) lv_obj_delete(root_);
    }

    /** Build LVGL widgets under root_. Called once after construction, before display. */
    virtual void enter()                                = 0;

    /** Periodic update for animation / dynamic state. Called once per UI tick. */
    virtual void update()                                {}

    virtual void on_button(buttons::ButtonEvent ev)      = 0;
    virtual void on_message(const protocol::Message&)    {}

    lv_obj_t* root() const { return root_; }

protected:
    lv_obj_t* root_ = nullptr;
};

}  // namespace devotchi::ui
