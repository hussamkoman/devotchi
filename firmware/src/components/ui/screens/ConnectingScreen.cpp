#include "screens/ConnectingScreen.hpp"

namespace devotchi::ui {

ConnectingScreen::ConnectingScreen(std::string label) : label_(std::move(label)) {}

void ConnectingScreen::enter() {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);

    auto* spinner = lv_spinner_create(root_);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    auto* label = lv_label_create(root_);
    lv_label_set_text(label, label_.c_str());
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 30);
}

void ConnectingScreen::on_button(buttons::ButtonEvent) {}

}  // namespace devotchi::ui
