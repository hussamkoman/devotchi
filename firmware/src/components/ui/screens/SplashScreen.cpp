#include "screens/SplashScreen.hpp"

namespace devotchi::ui {

void SplashScreen::enter() {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "Devotchi");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

    auto* sub = lv_label_create(root_);
    lv_label_set_text(sub, "booting...");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x808080), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 20);
}

void SplashScreen::on_button(buttons::ButtonEvent) {}

}  // namespace devotchi::ui
