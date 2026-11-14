#pragma once

#include "Display.hpp"
#include "Task.hpp"

#include <atomic>
#include <cstdint>

namespace devotchi::power {

struct BatteryStatus {
    uint8_t pct      = 0;
    bool    charging = false;
    bool    charged  = false;
};

class Power {
public:
    void init(display::Display& display);

    uint8_t battery_percent();
    bool    is_charging();
    bool    is_charged();

    void backlight(uint8_t pct);
    void enter_light_sleep(uint32_t ms);
    void enter_deep_sleep();

    BatteryStatus latest() const;

    // Called by PowerTask.
    void sample();

private:
    display::Display* display_ = nullptr;
    void*             adc_handle_  = nullptr;      // adc_oneshot_unit_handle_t
    void*             adc_cali_    = nullptr;      // adc_cali_handle_t (nullable)

    std::atomic<uint32_t> latest_packed_{0};
};

class PowerTask : public runtime::Task {
public:
    explicit PowerTask(Power& target);

protected:
    void run() override;

private:
    Power& target_;
};

}  // namespace devotchi::power
