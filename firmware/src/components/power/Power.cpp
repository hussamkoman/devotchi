#include "Power.hpp"

#include "esp_log.h"

namespace devotchi::power {

namespace {
constexpr const char* TAG = "Power";
}

void Power::init() {
    ESP_LOGI(TAG, "Power init (stub — wire up ledc backlight, ADC battery, esp_pm here)");
}

uint8_t Power::battery_percent()           { return 100; }
void    Power::backlight(uint8_t)          {}
void    Power::enter_light_sleep(uint32_t) {}
void    Power::enter_deep_sleep()          {}

}  // namespace devotchi::power
