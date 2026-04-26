#include "Buttons.hpp"

#include "Config.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by queue.h
#include "freertos/queue.h"
#include "iot_button.h"
#include "button_gpio.h"

namespace devotchi::buttons {

namespace {
constexpr const char* TAG = "Buttons";

button_handle_t make_button(gpio_num_t pin) {
    button_config_t btn_cfg = {
        .long_press_time  = config::buttons::LONG_PRESS_MS,
        .short_press_time = 0,                  // 0 = use Kconfig default
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num          = pin,
        .active_level      = 0,                 // active-low (hardware pull-up)
        .enable_power_save = false,
        .disable_pull      = true,              // hardware has 10kΩ pull-up + 100nF
    };
    button_handle_t handle = nullptr;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &handle));
    return handle;
}
}  // namespace

void Buttons::init() {
    queue_ = xQueueCreate(config::buttons::QUEUE_DEPTH, sizeof(ButtonEvent));
    configASSERT(queue_ != nullptr);

    approve_handle_ = make_button(config::pins::BTN_APPROVE);
    reject_handle_  = make_button(config::pins::BTN_REJECT);
    action_handle_  = make_button(config::pins::BTN_ACTION);

    auto register_for = [](void* opaque_handle, ButtonContext* ctx) {
        auto h = static_cast<button_handle_t>(opaque_handle);
        ESP_ERROR_CHECK(iot_button_register_cb(h, BUTTON_PRESS_DOWN,       NULL, &Buttons::on_press_cb,      ctx));
        ESP_ERROR_CHECK(iot_button_register_cb(h, BUTTON_LONG_PRESS_START, NULL, &Buttons::on_long_press_cb, ctx));
    };

    register_for(approve_handle_, &approve_ctx_);
    register_for(reject_handle_,  &reject_ctx_);
    register_for(action_handle_,  &action_ctx_);

    ESP_LOGI(TAG, "init — 3 buttons (approve=%d reject=%d action=%d)",
             config::pins::BTN_APPROVE, config::pins::BTN_REJECT, config::pins::BTN_ACTION);
}

bool Buttons::poll(ButtonEvent& out, uint32_t timeout_ms) {
    auto q = static_cast<QueueHandle_t>(queue_);
    return xQueueReceive(q, &out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void Buttons::on_press_cb(void* /*btn_handle*/, void* user_data) {
    auto* ctx = static_cast<ButtonContext*>(user_data);
    ButtonEvent ev{ctx->id, EventKind::Press};
    xQueueSend(static_cast<QueueHandle_t>(ctx->self->queue_), &ev, 0);
}

void Buttons::on_long_press_cb(void* /*btn_handle*/, void* user_data) {
    auto* ctx = static_cast<ButtonContext*>(user_data);
    ButtonEvent ev{ctx->id, EventKind::LongPress};
    xQueueSend(static_cast<QueueHandle_t>(ctx->self->queue_), &ev, 0);
}

}  // namespace devotchi::buttons
