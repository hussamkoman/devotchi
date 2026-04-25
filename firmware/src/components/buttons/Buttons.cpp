#include "Buttons.hpp"

#include "esp_log.h"

namespace devotchi::buttons {

namespace {
constexpr const char* TAG = "Buttons";
}

void Buttons::init() {
    ESP_LOGI(TAG, "Buttons init (stub — wire up espressif/button + FreeRTOS queue here)");
}

bool Buttons::poll(ButtonEvent&, uint32_t) {
    return false;
}

}  // namespace devotchi::buttons
