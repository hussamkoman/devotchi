#include "Task.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
#include "freertos/task.h"

namespace devotchi::runtime {

namespace {
constexpr const char* TAG = "Task";
}

Task::Task(Spec spec) : spec_(spec) {}

Task::~Task() {
    if (handle_) {
        vTaskDelete(static_cast<TaskHandle_t>(handle_));
    }
}

void Task::entry_trampoline(void* arg) {
    static_cast<Task*>(arg)->run();        // virtual dispatch into subclass
    // If run() returns, delete ourselves cleanly.
    auto* self = static_cast<Task*>(arg);
    self->handle_ = nullptr;
    vTaskDelete(nullptr);
}

void Task::spawn() {
    if (handle_) {
        ESP_LOGW(TAG, "spawn() ignored — task '%s' already running", spec_.name);
        return;
    }
    stop_requested_ = false;

    TaskHandle_t h = nullptr;
    BaseType_t ok = xTaskCreate(
        &Task::entry_trampoline,
        spec_.name,
        spec_.stack_bytes,
        this,
        spec_.priority,
        &h
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed for '%s' (stack=%lu, prio=%u)",
                 spec_.name, (unsigned long)spec_.stack_bytes, spec_.priority);
        return;
    }
    handle_ = h;
    ESP_LOGI(TAG, "spawned '%s' stack=%lu prio=%u",
             spec_.name, (unsigned long)spec_.stack_bytes, spec_.priority);
}

void Task::stop() {
    stop_requested_ = true;
}

bool Task::is_running() const {
    return handle_ != nullptr;
}

}  // namespace devotchi::runtime
