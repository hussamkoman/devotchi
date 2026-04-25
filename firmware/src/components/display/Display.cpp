#include "Display.hpp"

#include "esp_log.h"

namespace devotchi::display {

namespace {
constexpr const char* TAG = "Display";
}

void Display::init() {
    ESP_LOGI(TAG, "Display init (stub — wire up esp_lcd + LovyanGFX here)");
}

void Display::clear(uint16_t)         {}
void Display::flush()                 {}
void Display::set_brightness(uint8_t) {}
void Display::sleep()                 {}
void Display::wake()                  {}

}  // namespace devotchi::display
