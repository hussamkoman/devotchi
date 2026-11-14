#pragma once

#include "lvgl.h"

#include <cstdint>

namespace devotchi::ui {

/**
 * Persistent overlay drawn on lv_layer_top — battery, wifi, paired indicator.
 * Visible above any active screen. Hidden via show(false) for screens that
 * want a clean canvas (Splash, Provisioning, Pairing).
 */
class StatusBar {
public:
    void init();

    void show(bool visible);
    void update(uint8_t battery_pct, bool charging, bool wifi_connected, bool paired);

private:
    lv_obj_t* root_       = nullptr;
    lv_obj_t* batt_label_ = nullptr;
    lv_obj_t* wifi_label_ = nullptr;
    lv_obj_t* pair_dot_   = nullptr;
};

}  // namespace devotchi::ui
