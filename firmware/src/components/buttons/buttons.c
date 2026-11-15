#include "buttons.h"

#include "board_pins.h"

#include "button_gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "iot_button.h"

#include <stdint.h>

static const char *TAG = "buttons";

static const int PINS[BTN_COUNT] = {
    [BTN_APPROVE] = BOARD_PIN_BTN_APPROVE,
    [BTN_REJECT] = BOARD_PIN_BTN_REJECT,
    [BTN_ACTION] = BOARD_PIN_BTN_ACTION,
};

static const struct {
    button_event_t lib;
    btn_raw_t raw;
} EVENTS[] = {
    {BUTTON_PRESS_DOWN, BTN_RAW_DOWN},
    {BUTTON_PRESS_UP, BTN_RAW_UP},
};

#define EVENT_COUNT (sizeof(EVENTS) / sizeof(EVENTS[0]))

#ifdef CONFIG_DEVOTCHI_BTN_POWER_SAVE
#define POWER_SAVE true
#else
#define POWER_SAVE false
#endif

/* Written once in buttons_init before any button exists. */
static buttons_cb_t s_cb;

/* One context per (button, event) pair, passed as the library's user data. */
typedef struct {
    btn_id_t id;
    btn_raw_t raw;
} ctx_t;

static ctx_t s_ctx[BTN_COUNT][EVENT_COUNT];

static void on_event(void *handle, void *user)
{
    (void)handle;
    const ctx_t *ctx = user;
    s_cb(ctx->id, ctx->raw, (uint32_t)(esp_timer_get_time() / 1000));
}

esp_err_t buttons_init(buttons_cb_t cb)
{
    ESP_RETURN_ON_FALSE(cb != NULL, ESP_ERR_INVALID_ARG, TAG, "callback");
    s_cb = cb;

    /* Holds are timed by btn_fsm from DOWN and UP; the library's own long press is not used. */
    const button_config_t cfg = {0};
    for (int i = 0; i < BTN_COUNT; i++) {
        /*
         * Power-save mode stops the library's 5 ms polling timer while no
         * button is down, so automatic light sleep can happen; the library then
         * owns the GPIO level interrupt and the GPIO wake-up of these pins.
         */
        const button_gpio_config_t gpio = {
            .gpio_num = PINS[i],
            .active_level = 0,
            .enable_power_save = POWER_SAVE,
            .disable_pull = true,
        };
        button_handle_t handle = NULL;
        ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&cfg, &gpio, &handle), TAG, "gpio %d", PINS[i]);
        for (size_t e = 0; e < EVENT_COUNT; e++) {
            s_ctx[i][e] = (ctx_t){.id = (btn_id_t)i, .raw = EVENTS[e].raw};
            ESP_RETURN_ON_ERROR(iot_button_register_cb(handle, EVENTS[e].lib, NULL, on_event, &s_ctx[i][e]), TAG,
                                "register");
        }
    }
    ESP_LOGI(TAG, "ready, power save %s", POWER_SAVE ? "on" : "off");
    return ESP_OK;
}
