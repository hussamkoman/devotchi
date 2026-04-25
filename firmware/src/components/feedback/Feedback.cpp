#include "Feedback.hpp"

#include "Config.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace devotchi::feedback {

namespace {
constexpr const char* TAG = "Feedback";
}

void Feedback::init() {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << config::pins::LED_PWM,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(config::pins::LED_PWM, 0);

    ESP_LOGI(TAG, "init — LED on GPIO %d configured as output", config::pins::LED_PWM);
}

void Feedback::led_set(uint8_t pct) {
    gpio_set_level(config::pins::LED_PWM, pct > 0 ? 1 : 0);
}

void Feedback::buzz(uint32_t /*duration_ms*/) {
    // TODO: drive MOTOR_EN via LEDC once esp_driver_ledc is wired in.
}

}  // namespace devotchi::feedback
