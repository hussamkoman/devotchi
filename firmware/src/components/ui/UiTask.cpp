#include "Ui.hpp"

#include "Config.hpp"

#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
#include "freertos/task.h"

namespace devotchi::ui {

UiTask::UiTask(Ui& target)
    : runtime::Task(config::tasks::UI_RENDER), target_(target) {}

void UiTask::run() {
    while (!should_stop()) {
        target_.tick();
        vTaskDelay(pdMS_TO_TICKS(config::ui::TICK_INTERVAL_MS));
    }
}

}  // namespace devotchi::ui
