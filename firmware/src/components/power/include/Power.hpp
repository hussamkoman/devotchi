#pragma once

#include <cstdint>

namespace devotchi::power {

class Power {
public:
    void init();

    uint8_t battery_percent();            // 0-100
    void    backlight(uint8_t pct);       // 0-100, PWM duty
    void    enter_light_sleep(uint32_t ms);
    void    enter_deep_sleep();           // wake on button press only
};

}  // namespace devotchi::power
