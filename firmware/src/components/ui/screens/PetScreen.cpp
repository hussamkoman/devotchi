#include "screens/PetScreen.hpp"

#include "esp_log.h"

namespace devotchi::ui {

namespace {
constexpr const char* TAG = "PetScreen";
}

void PetScreen::enter(display::Display&) {
    ESP_LOGI(TAG, "PetScreen entered");
}

void PetScreen::tick(display::Display&) {}

void PetScreen::on_button(buttons::ButtonEvent) {}

void PetScreen::on_message(const protocol::Message&) {}

}  // namespace devotchi::ui
