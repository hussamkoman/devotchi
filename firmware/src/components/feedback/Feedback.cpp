#include "Feedback.hpp"

#include "esp_log.h"

namespace devotchi::feedback {

namespace {
constexpr const char* TAG = "Feedback";
}

void Feedback::init() {
    ESP_LOGI(TAG, "Feedback init (stub — wire up ledc PWM for LED + motor here)");
}

void Feedback::led_set(uint8_t)   {}
void Feedback::buzz(uint32_t)     {}

}  // namespace devotchi::feedback
