#pragma once

#include <cstdint>

namespace devotchi::feedback {

/** Drives the status LED and vibration motor via LEDC PWM. Not thread-safe. */
class Feedback {
public:
    void init();

    void led_set(uint8_t pct);                                  // immediate, 0..100
    void motor_set(uint8_t pct);                                // immediate, 0..100

    void flash(uint32_t duration_ms, uint8_t intensity = 100);  // non-blocking
    void buzz(uint32_t duration_ms, uint8_t intensity = 100);   // non-blocking

private:
    void* led_timer_   = nullptr;   // esp_timer_handle_t — opaque in header
    void* motor_timer_ = nullptr;

    static void led_off_cb(void* arg);
    static void motor_off_cb(void* arg);
};

}  // namespace devotchi::feedback
