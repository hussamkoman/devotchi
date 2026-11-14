#include "widgets/StatusBar.hpp"

#include <cstdio>

namespace devotchi::ui {

void StatusBar::init() {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 320, 18);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    batt_label_ = lv_label_create(root_);
    lv_label_set_text(batt_label_, "---%");
    lv_obj_set_style_text_color(batt_label_, lv_color_white(), 0);
    lv_obj_align(batt_label_, LV_ALIGN_LEFT_MID, 6, 0);

    wifi_label_ = lv_label_create(root_);
    lv_label_set_text(wifi_label_, "wifi: -");
    lv_obj_set_style_text_color(wifi_label_, lv_color_white(), 0);
    lv_obj_align(wifi_label_, LV_ALIGN_CENTER, 0, 0);

    pair_dot_ = lv_obj_create(root_);
    lv_obj_remove_style_all(pair_dot_);
    lv_obj_set_size(pair_dot_, 8, 8);
    lv_obj_set_style_radius(pair_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(pair_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pair_dot_, lv_color_hex(0x404040), 0);
    lv_obj_align(pair_dot_, LV_ALIGN_RIGHT_MID, -8, 0);
}

void StatusBar::show(bool visible) {
    if (!root_) return;
    if (visible) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void StatusBar::update(uint8_t battery_pct, bool charging, bool wifi_connected, bool paired) {
    if (batt_label_) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), charging ? "%u%%+" : "%u%%", battery_pct);
        lv_label_set_text(batt_label_, buf);
    }
    if (wifi_label_) {
        lv_label_set_text(wifi_label_, wifi_connected ? "wifi: on" : "wifi: off");
        lv_obj_set_style_text_color(wifi_label_,
            wifi_connected ? lv_color_white() : lv_color_hex(0x808080), 0);
    }
    if (pair_dot_) {
        lv_obj_set_style_bg_color(pair_dot_,
            paired ? lv_color_hex(0x40FF40) : lv_color_hex(0x404040), 0);
    }
}

}  // namespace devotchi::ui
