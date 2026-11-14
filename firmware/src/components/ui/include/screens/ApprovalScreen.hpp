#pragma once

#include "Protocol.hpp"
#include "Screen.hpp"

#include <functional>
#include <string>

namespace devotchi::ui {

/**
 * Modal screen for an inbound approval/permission request. The user picks
 * APPROVE or REJECT; the screen invokes respond_cb_ with the decision and
 * the App takes care of routing the response over the WebSocket and going
 * back to PetScreen.
 */
class ApprovalScreen : public Screen {
public:
    enum class Kind { Approval, Permission };

    using RespondCallback = std::function<void(const std::string& id, Kind kind, bool approved)>;

    ApprovalScreen(std::string id,
                   Kind        kind,
                   std::string title,
                   std::string body,
                   uint32_t    timeout_ms,
                   RespondCallback respond);

    void enter() override;
    void update() override;
    void on_button(buttons::ButtonEvent ev) override;
    void on_message(const protocol::Message& msg) override;

    const std::string& id() const { return id_; }

private:
    void respond(bool approved);

    std::string     id_;
    Kind            kind_;
    std::string     title_;
    std::string     body_;
    uint32_t        timeout_ms_;
    RespondCallback respond_;

    uint64_t  enter_ms_ = 0;
    bool      done_     = false;
    lv_obj_t* countdown_label_ = nullptr;
};

}  // namespace devotchi::ui
