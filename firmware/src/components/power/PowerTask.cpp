#include "Power.hpp"

#include "Config.hpp"

#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
#include "freertos/task.h"

namespace devotchi::power {

PowerTask::PowerTask(Power& target)
    : runtime::Task(config::tasks::POWER_SAMPLER), target_(target) {}

void PowerTask::run() {
    while (!should_stop()) {
        target_.sample();
        vTaskDelay(pdMS_TO_TICKS(config::power::BATT_SAMPLE_INTERVAL_MS));
    }
}

}  // namespace devotchi::power
