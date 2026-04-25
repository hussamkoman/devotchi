#pragma once

#include <cstdint>

namespace devotchi::feedback {

/** Drives the status LED and vibration motor. Synchronous; not thread-safe. */
class Feedback {
public:
    void init();

    void led_set(uint8_t pct);            // 0-100
    void buzz(uint32_t duration_ms);      // fire-and-forget vibration
};

}  // namespace devotchi::feedback
