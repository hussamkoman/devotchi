#pragma once

/*
 * Devotchi board, ESP32-S3-MINI-1-N4R2. The schematic is authoritative.
 *
 * Never configured: GPIO7 (floating; the fourth switch is the reset button on
 * EN), GPIO19/20 (native USB-Serial-JTAG), GPIO43/44 (UART0 test points).
 */

/* Battery: 100k/100k divider, Vbatt = 2 x Vadc. */
#define BOARD_PIN_BAT_ADC        2
#define BOARD_BAT_ADC_UNIT       ADC_UNIT_1
#define BOARD_BAT_ADC_CHANNEL    ADC_CHANNEL_1
#define BOARD_BAT_DIVIDER        2

/* Buttons: low = pressed, external 10k pull-up + 100 nF, internal pulls off. */
#define BOARD_PIN_BTN_APPROVE    4
#define BOARD_PIN_BTN_REJECT     5
#define BOARD_PIN_BTN_ACTION     6

/* Display: ST7789 on SPI2, write-only. */
#define BOARD_PIN_DISP_RST       8
#define BOARD_PIN_DISP_DC        9
#define BOARD_PIN_DISP_CS        10
#define BOARD_PIN_DISP_MOSI      11
#define BOARD_PIN_DISP_SCLK      12
#define BOARD_PIN_DISP_BL        13
#define BOARD_DISP_SPI_HOST      SPI2_HOST
#define BOARD_DISP_H_RES         320
#define BOARD_DISP_V_RES         240

/* TP4056 status outputs: open drain, low = active, internal pull-up required. */
#define BOARD_PIN_PWR_STDBY      17
#define BOARD_PIN_PWR_CHRG       18

/* High = on, low-side MOSFETs. */
#define BOARD_PIN_MOTOR          37
#define BOARD_PIN_LED            38

/*
 * LEDC. Every timer is clocked from the crystal so dynamic frequency scaling
 * cannot shift the PWM frequency.
 */
#define BOARD_LEDC_MODE          LEDC_LOW_SPEED_MODE
#define BOARD_LEDC_CLK           LEDC_USE_XTAL_CLK
#define BOARD_LEDC_FREQ_HZ       5000
#define BOARD_LEDC_DUTY_BITS     10
#define BOARD_LEDC_DUTY_MAX      ((1U << BOARD_LEDC_DUTY_BITS) - 1U)

#define BOARD_LEDC_TIMER_FEEDBACK   LEDC_TIMER_0
#define BOARD_LEDC_CH_LED           LEDC_CHANNEL_0
#define BOARD_LEDC_CH_MOTOR         LEDC_CHANNEL_1

#define BOARD_LEDC_TIMER_BACKLIGHT  LEDC_TIMER_1
#define BOARD_LEDC_CH_BACKLIGHT     LEDC_CHANNEL_2
