#include "App.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
#include "freertos/task.h"

namespace devotchi {

namespace {
constexpr const char* TAG = "App";
}

void App::run() {
    ESP_LOGI(TAG, "Devotchi booting");

    storage_.init();
    display_.init();
    ui_.init(display_);
    buttons_.init();
    feedback_.init();
    power_.init();
    wifi_.init(storage_);
    ws_.init(storage_);

    ESP_LOGI(TAG, "Devotchi ready");

    while (true) {
        ui_.tick();
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

}  // namespace devotchi
