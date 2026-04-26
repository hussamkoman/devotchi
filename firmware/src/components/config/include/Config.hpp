#pragma once

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
    constexpr gpio_num_t BAT_ADC      = GPIO_NUM_1;
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
    constexpr const char* DEFAULT_SERVER_URL = "wss://devotchi.example/ws";
    constexpr uint32_t WS_PING_INTERVAL_MS   = 30'000;
    constexpr uint32_t WS_RECONNECT_DELAY_MS = 5'000;
}

}  // namespace devotchi::config
