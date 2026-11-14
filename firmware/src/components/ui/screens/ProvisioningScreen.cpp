#include "screens/ProvisioningScreen.hpp"

namespace devotchi::ui {

void ProvisioningScreen::enter() {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 12, LV_PART_MAIN);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    auto* body = lv_label_create(root_);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 280);
    lv_label_set_text(body,
        "1. Open ESP BLE Provisioning app\n"
        "2. Pair with: DEVOTCHI\n"
        "3. PIN: abcd1234\n"
        "4. Choose your WiFi");
    lv_obj_set_style_text_color(body, lv_color_white(), 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 0);

    auto* spinner = lv_spinner_create(root_);
    lv_obj_set_size(spinner, 30, 30);
    lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void ProvisioningScreen::on_button(buttons::ButtonEvent) {}

}  // namespace devotchi::ui
