#pragma once

#include "Task.hpp"
#include "driver/gpio.h"
#include <cstddef>
#include <cstdint>

namespace devotchi::config {

namespace pins {
    constexpr gpio_num_t DISP_SCLK    = GPIO_NUM_12;
    constexpr gpio_num_t DISP_MOSI    = GPIO_NUM_11;
    constexpr gpio_num_t DISP_CS      = GPIO_NUM_10;
    constexpr gpio_num_t DISP_DC      = GPIO_NUM_9;
    constexpr gpio_num_t DISP_RST     = GPIO_NUM_8;
    constexpr gpio_num_t DISP_BL      = GPIO_NUM_13;

    constexpr gpio_num_t BTN_APPROVE  = GPIO_NUM_4;
    constexpr gpio_num_t BTN_REJECT   = GPIO_NUM_5;
    constexpr gpio_num_t BTN_ACTION   = GPIO_NUM_6;

    constexpr gpio_num_t LED_PWM      = GPIO_NUM_38;
    constexpr gpio_num_t MOTOR_EN     = GPIO_NUM_37;

    constexpr gpio_num_t PWR_CHRG     = GPIO_NUM_18;
    constexpr gpio_num_t PWR_STDBY    = GPIO_NUM_17;
    constexpr gpio_num_t BAT_ADC      = GPIO_NUM_2;
}

namespace display {
    constexpr int      WIDTH      = 320;
    constexpr int      HEIGHT     = 240;
    constexpr uint32_t SPI_HZ     = 40'000'000;
    constexpr uint32_t BL_PWM_HZ  = 5'000;
}

namespace ui {
    constexpr uint32_t SLEEP_TIMEOUT_MS_DEFAULT = 30'000;
    constexpr uint8_t  BRIGHTNESS_DEFAULT       = 80;
    constexpr uint32_t TICK_INTERVAL_MS         = 33;  // ~30 FPS
}

namespace feedback {
    constexpr uint32_t PWM_HZ       = 5'000;
    constexpr uint8_t  PWM_RES_BITS = 10;     // duty range 0..1023
}

namespace buttons {
    constexpr uint32_t LONG_PRESS_MS  = 1500;
    constexpr std::size_t QUEUE_DEPTH = 8;
}

namespace network {
    constexpr const char* SERVER_HOST        = "10.0.0.120";
    constexpr uint16_t    SERVER_PORT        = 8080;
    constexpr uint32_t    HTTP_TIMEOUT_MS    = 10'000;
    constexpr uint32_t    PAIR_CODE_LEN      = 6;
    constexpr uint32_t    WS_PING_INTERVAL_MS   = 30'000;
    constexpr uint32_t    WS_RECONNECT_DELAY_MS = 5'000;
    constexpr const char* PROV_DEVICE_NAME  = "DEVOTCHI";
    constexpr const char* PROV_POP          = "abcd1234";
}

namespace power {
    constexpr int      BATT_ADC_CHANNEL = 1;          // GPIO 2 = ADC1_CH1 on ESP32-S3
    constexpr uint32_t BATT_VMIN_MV     = 3300;       // 0% at 3.3V
    constexpr uint32_t BATT_VMAX_MV     = 4100;       // 100% at 4.1V
    constexpr uint32_t BATT_DIVIDER_NUM = 2;          // resistor divider 2:1 (verify against schematic)
    constexpr uint32_t BATT_DIVIDER_DEN = 1;
    constexpr uint32_t BATT_SAMPLE_INTERVAL_MS = 5'000;
}

namespace pairing {
    // Alphabet excludes 0/O/I/L/1 to avoid ambiguity (matches server spec).
    constexpr const char* ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
}

namespace tasks {
    inline constexpr runtime::Task::Spec UI_RENDER = {
        "ui_render",
        8192,
        4,
    };
    inline constexpr runtime::Task::Spec POWER_SAMPLER = {
        "pwr_sample",
        2048,
        2,
    };
}

}  // namespace devotchi::config
