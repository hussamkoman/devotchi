#pragma once

#include "Screen.hpp"

#include <functional>

namespace devotchi::ui {

/**
 * Three-item settings menu. APPROVE/REJECT navigate, ACTION cycles the
 * highlighted item's value (or confirms factory reset). Long ACTION exits.
 */
class SettingsScreen : public Screen {
public:
    using BrightnessCallback = std::function<void(uint8_t pct)>;
    using SleepCallback      = std::function<void(uint32_t ms)>;
    using FactoryResetCallback = std::function<void()>;
    using ExitCallback         = std::function<void()>;

    SettingsScreen(uint8_t initial_brightness,
                   uint32_t initial_sleep_ms,
                   BrightnessCallback   on_brightness,
                   SleepCallback        on_sleep,
                   FactoryResetCallback on_reset,
                   ExitCallback         on_exit);

    void enter() override;
    void on_button(buttons::ButtonEvent ev) override;

private:
    void redraw();
    void cycle_current();

    uint8_t  brightness_;
    uint32_t sleep_ms_;
    int      cursor_       = 0;
    bool     reset_armed_  = false;

    BrightnessCallback   on_brightness_;
    SleepCallback        on_sleep_;
    FactoryResetCallback on_reset_;
    ExitCallback         on_exit_;

    lv_obj_t* rows_[3]{};
};

}  // namespace devotchi::ui
