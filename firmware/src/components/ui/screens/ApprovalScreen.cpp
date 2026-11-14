#include "screens/ApprovalScreen.hpp"

#include "esp_timer.h"

#include <cstdio>

namespace devotchi::ui {

ApprovalScreen::ApprovalScreen(std::string id,
                               Kind kind,
                               std::string title,
                               std::string body,
                               uint32_t timeout_ms,
                               RespondCallback respond)
    : id_(std::move(id)),
      kind_(kind),
      title_(std::move(title)),
      body_(std::move(body)),
      timeout_ms_(timeout_ms),
      respond_(std::move(respond)) {}

void ApprovalScreen::enter() {
    enter_ms_ = esp_timer_get_time() / 1000ULL;

    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x101020), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 12, LV_PART_MAIN);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, title_.c_str());
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFD040), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    auto* body = lv_label_create(root_);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 280);
    lv_label_set_text(body, body_.c_str());
    lv_obj_set_style_text_color(body, lv_color_white(), 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -10);

    countdown_label_ = lv_label_create(root_);
    lv_label_set_text(countdown_label_, "");
    lv_obj_set_style_text_color(countdown_label_, lv_color_hex(0x808080), 0);
    lv_obj_align(countdown_label_, LV_ALIGN_BOTTOM_MID, 0, -28);

    auto* hint = lv_label_create(root_);
    lv_label_set_text(hint, "REJECT  X        Y  APPROVE");
    lv_obj_set_style_text_color(hint, lv_color_white(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void ApprovalScreen::update() {
    if (done_ || !countdown_label_) return;

    if (timeout_ms_ == 0) return;

    const uint64_t now = esp_timer_get_time() / 1000ULL;
    const int64_t remaining_ms = static_cast<int64_t>(timeout_ms_) -
                                  static_cast<int64_t>(now - enter_ms_);
    if (remaining_ms <= 0) {
        respond(false);
        return;
    }
    char buf[16];
    const int sec = static_cast<int>(remaining_ms / 1000);
    std::snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
    lv_label_set_text(countdown_label_, buf);
}

void ApprovalScreen::on_button(buttons::ButtonEvent ev) {
    using namespace buttons;
    if (ev.kind != EventKind::Press || done_) return;

    switch (ev.id) {
        case ButtonId::Approve: respond(true);  break;
        case ButtonId::Reject:  respond(false); break;
        case ButtonId::Action:  break;   // ignored
    }
}

void ApprovalScreen::on_message(const protocol::Message& msg) {
    if (auto* dis = std::get_if<protocol::DismissMessage>(&msg)) {
        if (dis->id == id_) {
            done_ = true;
            if (respond_) respond_(id_, kind_, false);
        }
    }
}

void ApprovalScreen::respond(bool approved) {
    if (done_) return;
    done_ = true;
    if (respond_) respond_(id_, kind_, approved);
}

}  // namespace devotchi::ui
