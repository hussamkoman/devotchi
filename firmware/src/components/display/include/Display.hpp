#pragma once

#include <cstdint>

namespace devotchi::display {

class Display {
public:
    void init();

    void clear(uint16_t color = 0x0000);
    void flush();

    void set_brightness(uint8_t pct);
    void sleep();
    void wake();
};

}  // namespace devotchi::display
