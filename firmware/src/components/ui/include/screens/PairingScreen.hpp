#pragma once

#include "Screen.hpp"

#include <functional>
#include <string>

namespace devotchi::ui {

/**
 * Six-character pairing code entry. APPROVE/REJECT cycle the active char,
 * ACTION advances; long ACTION submits. On submit, the screen calls the
 * provided submit_cb_ with the assembled code. The App owns the network call
 * and decides what to do next.
 */
class PairingScreen : public Screen {
public:
    using SubmitCallback = std::function<void(const std::string& code)>;

    explicit PairingScreen(SubmitCallback on_submit);

    void enter() override;
    void update() override;
    void on_button(buttons::ButtonEvent ev) override;

    void show_error(const char* msg);

private:
    void redraw_chars();

    SubmitCallback submit_cb_;
    char           code_[7]{};       // 6 chars + null
    int            pos_ = 0;

    lv_obj_t* labels_[6]{};
    lv_obj_t* hint_   = nullptr;
    lv_obj_t* error_  = nullptr;
    uint32_t  error_until_ms_ = 0;
};

}  // namespace devotchi::ui
