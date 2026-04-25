#include "Ui.hpp"

#include "screens/PetScreen.hpp"

#include "esp_log.h"

namespace devotchi::ui {

namespace {
constexpr const char* TAG = "Ui";
}

void Ui::init(display::Display& display) {
    display_ = &display;
    ESP_LOGI(TAG, "Ui init");
    show(std::make_unique<PetScreen>());
}

void Ui::show(std::unique_ptr<Screen> screen) {
    if (current_) current_->exit();
    current_ = std::move(screen);
    if (current_ && display_) current_->enter(*display_);
}

void Ui::tick() {
    if (current_ && display_) current_->tick(*display_);
}

void Ui::on_button(buttons::ButtonEvent ev) {
    if (current_) current_->on_button(ev);
}

void Ui::on_message(const protocol::Message& msg) {
    if (current_) current_->on_message(msg);
}

}  // namespace devotchi::ui
