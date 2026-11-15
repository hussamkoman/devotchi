#include "feedback.h"

#include "board_pins.h"
#include "storage.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "feedback";

/* Held for a few microseconds by every operation; the bound only guards against a stuck holder. */
#define LOCK_TIMEOUT_MS  20
#define MAX_REPORTS      2

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;
static feedback_step_cb_t s_cb;
static fb_seq_t s_seq;
static uint8_t s_led_pct = UINT8_MAX;
static uint8_t s_motor_pct = UINT8_MAX;

typedef struct {
    feedback_step_t items[MAX_REPORTS];
    int count;
} reports_t;

static void set_channel(ledc_channel_t ch, uint8_t pct, uint8_t *cache)
{
    if (*cache == pct) {
        return;
    }
    *cache = pct;
    ledc_set_duty(BOARD_LEDC_MODE, ch, (uint32_t)pct * BOARD_LEDC_DUTY_MAX / 100U);
    ledc_update_duty(BOARD_LEDC_MODE, ch);
}

static void set_outputs(uint8_t led_pct, uint8_t motor_pct)
{
    set_channel(BOARD_LEDC_CH_LED, led_pct, &s_led_pct);
    set_channel(BOARD_LEDC_CH_MOTOR, motor_pct, &s_motor_pct);
}

static void add_report(reports_t *r, fb_seq_id_t pattern, uint8_t step, uint8_t led, uint8_t motor, uint32_t now)
{
    if (r->count < MAX_REPORTS) {
        r->items[r->count++] = (feedback_step_t){pattern, step, led, motor, now};
    }
}

static void send_reports(const reports_t *r)
{
    if (s_cb == NULL) {
        return;
    }
    for (int i = 0; i < r->count; i++) {
        s_cb(&r->items[i]);
    }
}

/* Caller holds s_lock. */
static void run_step(reports_t *r)
{
    const int64_t now_us = esp_timer_get_time();
    const uint32_t now_ms = (uint32_t)(now_us / 1000);
    const fb_seq_id_t before = fb_seq_current(&s_seq);
    fb_seq_out_t out;
    fb_seq_poll(&s_seq, now_ms, &out);
    set_outputs(out.led_pct, out.motor_pct);
    esp_timer_stop(s_timer);

    if (out.done) {
        if (before != FB_SEQ_COUNT) {
            add_report(r, before, FEEDBACK_STEP_END, 0, 0, now_ms);
        }
        return;
    }
    if (out.step_started) {
        add_report(r, before, out.step, out.led_pct, out.motor_pct, now_ms);
    }
    /* Arming in microseconds keeps the next poll from landing just before the millisecond boundary. */
    const uint64_t delay_us = (uint64_t)(out.next_deadline_ms - now_ms) * 1000U - (uint64_t)(now_us % 1000);
    if (esp_timer_start_once(s_timer, delay_us) != ESP_OK) {
        ESP_LOGE(TAG, "timer");
    }
}

static void timer_cb(void *arg)
{
    (void)arg;
    reports_t r = {0};
    /* A busy lock means play/stop is running, and it re-arms the timer itself. */
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    run_step(&r);
    xSemaphoreGive(s_lock);
    send_reports(&r);
}

bool feedback_play_for(fb_seq_id_t pattern, uint32_t duration_ms)
{
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    reports_t r = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    const fb_seq_id_t before = fb_seq_current(&s_seq);
    const bool started = fb_seq_start(&s_seq, pattern, duration_ms, now_ms);
    if (started) {
        if (before != FB_SEQ_COUNT) {
            add_report(&r, before, FEEDBACK_STEP_END, 0, 0, now_ms);
        }
        run_step(&r);
    }
    xSemaphoreGive(s_lock);
    send_reports(&r);
    return started;
}

bool feedback_play(fb_seq_id_t pattern)
{
    return feedback_play_for(pattern, 0);
}

void feedback_stop(void)
{
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    reports_t r = {0};
    const fb_seq_id_t before = fb_seq_current(&s_seq);
    fb_seq_stop(&s_seq);
    esp_timer_stop(s_timer);
    set_outputs(0, 0);
    if (before != FB_SEQ_COUNT) {
        add_report(&r, before, FEEDBACK_STEP_END, 0, 0, (uint32_t)(esp_timer_get_time() / 1000));
    }
    xSemaphoreGive(s_lock);
    send_reports(&r);
}

fb_seq_id_t feedback_current(void)
{
    fb_seq_id_t id = FB_SEQ_COUNT;
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE) {
        id = fb_seq_current(&s_seq);
        xSemaphoreGive(s_lock);
    }
    return id;
}

void feedback_reload_settings(void)
{
    const uint16_t haptic = storage_get_setting(STORAGE_SETTING_HAPTIC);
    const bool led = storage_get_setting(STORAGE_SETTING_LED) != 0;
    const bool clicks = storage_get_setting(STORAGE_SETTING_CLICKS) != 0;
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    fb_seq_set_output(&s_seq, haptic <= FB_SEQ_HAPTIC_FULL ? (fb_seq_haptic_t)haptic : FB_SEQ_HAPTIC_FULL, led);
    fb_seq_set_clicks(&s_seq, clicks);
    reports_t r = {0};
    if (fb_seq_running(&s_seq)) {
        run_step(&r);
    }
    xSemaphoreGive(s_lock);
    send_reports(&r);
}

static esp_err_t config_channel(ledc_channel_t ch, int gpio)
{
    const ledc_channel_config_t cfg = {
        .gpio_num = gpio,
        .speed_mode = BOARD_LEDC_MODE,
        .channel = ch,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_LEDC_TIMER_FEEDBACK,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    };
    return ledc_channel_config(&cfg);
}

esp_err_t feedback_init(feedback_step_cb_t cb)
{
    s_cb = cb;
    const ledc_timer_config_t timer = {
        .speed_mode = BOARD_LEDC_MODE,
        .duty_resolution = BOARD_LEDC_DUTY_BITS,
        .timer_num = BOARD_LEDC_TIMER_FEEDBACK,
        .freq_hz = BOARD_LEDC_FREQ_HZ,
        .clk_cfg = BOARD_LEDC_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
    ESP_RETURN_ON_ERROR(config_channel(BOARD_LEDC_CH_LED, BOARD_PIN_LED), TAG, "led channel");
    ESP_RETURN_ON_ERROR(config_channel(BOARD_LEDC_CH_MOTOR, BOARD_PIN_MOTOR), TAG, "motor channel");
    s_led_pct = 0;
    s_motor_pct = 0;

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "feedback",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_timer), TAG, "timer");

    fb_seq_init(&s_seq, CONFIG_DEVOTCHI_MOTOR_MAX_DUTY_PCT);
    feedback_reload_settings();
    return ESP_OK;
}
