#pragma once

#include "Protocol.hpp"
#include "Screen.hpp"

#include "lvgl.h"

#include <string>

namespace devotchi::ui {

class PetScreen : public Screen {
public:
    void enter() override;
    void update() override;
    void on_button(buttons::ButtonEvent ev) override;
    void on_message(const protocol::Message& msg) override;

private:
    void rebuild_face();
    void show_toast(const std::string& text);

    protocol::Mood mood_ = protocol::Mood::Idle;
    std::string    status_text_;

    lv_obj_t* face_root_   = nullptr;     // container we wipe and rebuild per mood change
    lv_obj_t* mood_label_  = nullptr;
    lv_obj_t* status_label_= nullptr;

    lv_obj_t* toast_       = nullptr;
    uint64_t  toast_until_ms_ = 0;
};

}  // namespace devotchi::ui
