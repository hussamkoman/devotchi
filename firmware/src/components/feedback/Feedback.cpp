#include "Feedback.hpp"

#include "Config.hpp"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace devotchi::feedback {

namespace {
constexpr const char *TAG = "Feedback";

constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t LED_CH = LEDC_CHANNEL_0;
constexpr ledc_channel_t MOTOR_CH = LEDC_CHANNEL_1;
constexpr uint32_t DUTY_MAX = (1u << config::feedback::PWM_RES_BITS) - 1u;

uint32_t pct_to_duty(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  return (static_cast<uint32_t>(pct) * DUTY_MAX) / 100u;
}

void set_duty(ledc_channel_t channel, uint8_t pct) {
  ledc_set_duty(SPEED_MODE, channel, pct_to_duty(pct));
  ledc_update_duty(SPEED_MODE, channel);
}
} // namespace

void Feedback::init() {
  ledc_timer_config_t timer = {
      .speed_mode = SPEED_MODE,
      .duty_resolution =
          static_cast<ledc_timer_bit_t>(config::feedback::PWM_RES_BITS),
      .timer_num = TIMER,
      .freq_hz = config::feedback::PWM_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
      .deconfigure = false,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t led_ch = {
      .gpio_num = config::pins::LED_PWM,
      .speed_mode = SPEED_MODE,
      .channel = LED_CH,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = TIMER,
      .duty = 0,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {},
      .deconfigure = false,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&led_ch));

  ledc_channel_config_t motor_ch = {
      .gpio_num = config::pins::MOTOR_EN,
      .speed_mode = SPEED_MODE,
      .channel = MOTOR_CH,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = TIMER,
      .duty = 0,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {},
      .deconfigure = false,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&motor_ch));

  esp_timer_create_args_t led_args = {
      .callback = &Feedback::led_off_cb,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "feedback_led_off",
      .skip_unhandled_events = false,
  };
  ESP_ERROR_CHECK(esp_timer_create(
      &led_args, reinterpret_cast<esp_timer_handle_t *>(&led_timer_)));

  esp_timer_create_args_t motor_args = {
      .callback = &Feedback::motor_off_cb,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "feedback_motor_off",
      .skip_unhandled_events = false,
  };
  ESP_ERROR_CHECK(esp_timer_create(
      &motor_args, reinterpret_cast<esp_timer_handle_t *>(&motor_timer_)));

  ESP_LOGI(TAG,
           "init — LED PWM on GPIO %d (ch %d), motor PWM on GPIO %d (ch %d)",
           config::pins::LED_PWM, LED_CH, config::pins::MOTOR_EN, MOTOR_CH);
}

void Feedback::led_set(uint8_t pct) { set_duty(LED_CH, pct); }

void Feedback::motor_set(uint8_t pct) { set_duty(MOTOR_CH, pct); }

void Feedback::flash(uint32_t duration_ms, uint8_t intensity) {
  auto handle = static_cast<esp_timer_handle_t>(led_timer_);
  esp_timer_stop(handle);
  led_set(intensity);
  ESP_ERROR_CHECK(esp_timer_start_once(
      handle, static_cast<uint64_t>(duration_ms) * 1000ULL));
}

void Feedback::buzz(uint32_t duration_ms, uint8_t intensity) {
  auto handle = static_cast<esp_timer_handle_t>(motor_timer_);
  esp_timer_stop(handle);
  motor_set(intensity);
  ESP_ERROR_CHECK(esp_timer_start_once(
      handle, static_cast<uint64_t>(duration_ms) * 1000ULL));
}

void Feedback::led_off_cb(void *arg) {
  static_cast<Feedback *>(arg)->led_set(0);
}

void Feedback::motor_off_cb(void *arg) {
  static_cast<Feedback *>(arg)->motor_set(0);
}

} // namespace devotchi::feedback
