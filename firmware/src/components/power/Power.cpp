#include "Power.hpp"

#include "Config.hpp"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"

namespace devotchi::power {

namespace {
constexpr const char* TAG = "Power";

uint32_t pack(BatteryStatus s) {
    return static_cast<uint32_t>(s.pct)
         | (static_cast<uint32_t>(s.charging) << 8)
         | (static_cast<uint32_t>(s.charged)  << 9);
}

BatteryStatus unpack(uint32_t v) {
    BatteryStatus s;
    s.pct      = static_cast<uint8_t>(v & 0xFF);
    s.charging = (v >> 8) & 1;
    s.charged  = (v >> 9) & 1;
    return s;
}
}  // namespace

void Power::init(display::Display& display) {
    display_ = &display;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    adc_oneshot_unit_handle_t handle = nullptr;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &handle));

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle,
        static_cast<adc_channel_t>(config::power::BATT_ADC_CHANNEL), &chan_cfg));

    adc_handle_ = handle;

    // Curve-fitting calibration uses chip eFuse data for accurate raw→mV.
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = ADC_UNIT_1;
    cali_cfg.atten    = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_cali_handle_t cali = nullptr;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) == ESP_OK) {
        adc_cali_ = cali;
    }

    // CHRG and STDBY on TP4056 are open-drain — internal pull-up to 3.3V so
    // they read HIGH when the charger isn't actively pulling LOW.
    gpio_config_t io = {};
    io.intr_type    = GPIO_INTR_DISABLE;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pin_bit_mask = (1ULL << config::pins::PWR_CHRG) | (1ULL << config::pins::PWR_STDBY);
    ESP_ERROR_CHECK(gpio_config(&io));

    sample();
    ESP_LOGI(TAG, "init — battery ADC ch%d, charge GPIOs %d/%d",
             config::power::BATT_ADC_CHANNEL,
             config::pins::PWR_CHRG, config::pins::PWR_STDBY);
}

uint8_t Power::battery_percent() { return latest().pct; }

bool Power::is_charging() { return gpio_get_level(config::pins::PWR_CHRG) == 0; }
bool Power::is_charged()  { return gpio_get_level(config::pins::PWR_STDBY) == 0; }

void Power::backlight(uint8_t pct) {
    if (display_) display_->set_brightness(pct);
}

void Power::enter_light_sleep(uint32_t ms) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000ULL);
    esp_light_sleep_start();
}

void Power::enter_deep_sleep() {
    const uint64_t mask = (1ULL << config::pins::BTN_APPROVE) |
                          (1ULL << config::pins::BTN_REJECT)  |
                          (1ULL << config::pins::BTN_ACTION);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

BatteryStatus Power::latest() const {
    return unpack(latest_packed_.load(std::memory_order_relaxed));
}

void Power::sample() {
    BatteryStatus s;
    s.charging = is_charging();
    s.charged  = is_charged();

    if (adc_handle_) {
        auto handle = static_cast<adc_oneshot_unit_handle_t>(adc_handle_);

        // Discard a couple of warm-up samples — first reads after a long idle
        // can be wrong because the S/H cap hasn't reached equilibrium with
        // the high-impedance source (50 kΩ from the 100k+100k divider).
        int discard = 0;
        for (int i = 0; i < 4; ++i) {
            adc_oneshot_read(handle, static_cast<adc_channel_t>(config::power::BATT_ADC_CHANNEL), &discard);
            esp_rom_delay_us(200);
        }

        int sum = 0;
        constexpr int N = 32;
        for (int i = 0; i < N; ++i) {
            int raw = 0;
            adc_oneshot_read(handle, static_cast<adc_channel_t>(config::power::BATT_ADC_CHANNEL), &raw);
            sum += raw;
            esp_rom_delay_us(100);
        }
        const int raw_avg = sum / N;

        uint32_t adc_mv = 0;
        if (adc_cali_) {
            int mv = 0;
            if (adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(adc_cali_),
                                        raw_avg, &mv) == ESP_OK) {
                adc_mv = static_cast<uint32_t>(mv);
            }
        }
        if (adc_mv == 0) {
            adc_mv = static_cast<uint32_t>(raw_avg) * 3100u / 4095u;
        }

        const uint32_t batt_mv = adc_mv * config::power::BATT_DIVIDER_NUM
                                       / config::power::BATT_DIVIDER_DEN;

        ESP_LOGI(TAG, "battery: raw=%d adc_mv=%lu batt_mv=%lu chrg=%d stdby=%d",
                 raw_avg,
                 static_cast<unsigned long>(adc_mv),
                 static_cast<unsigned long>(batt_mv),
                 s.charging ? 1 : 0,
                 s.charged  ? 1 : 0);

        if (batt_mv <= config::power::BATT_VMIN_MV) {
            s.pct = 0;
        } else if (batt_mv >= config::power::BATT_VMAX_MV) {
            s.pct = 100;
        } else {
            const uint32_t span = config::power::BATT_VMAX_MV - config::power::BATT_VMIN_MV;
            s.pct = static_cast<uint8_t>(((batt_mv - config::power::BATT_VMIN_MV) * 100u) / span);
        }
    }

    latest_packed_.store(pack(s), std::memory_order_relaxed);
}

}  // namespace devotchi::power
