#include "power.h"

#include "board_pins.h"
#include "storage.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "power";

#define ADC_PERIOD_MS        2000
#define ADC_DISCARD          4
#define ADC_AVERAGE          32
/* Settling for the sample-and-hold capacitor behind the 50 kOhm divider source. */
#define ADC_DISCARD_DELAY_US 200
#define ADC_SAMPLE_DELAY_US  100
/* Uncalibrated full scale at 12 dB attenuation. */
#define ADC_UNCAL_FULL_MV    3100
#define ADC_MAX_RAW          4095
/* Storm guard: a floating or oscillating CHRG line must not keep the CPU in the ISR. */
#define EDGE_STORM_LIMIT     50
#define EDGE_WINDOW_MS       10000
#define EDGE_RING_SLOTS      10
#define LOCK_TIMEOUT_MS      100

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static SemaphoreHandle_t s_adc_lock;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static power_cb_t s_cb;

/* Shared with the CHRG ISR under s_edge_mux. */
static portMUX_TYPE s_edge_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_edges;
static bool s_edge_storm;
static int64_t s_edge_first_us;
static int64_t s_edge_storm_us;

/* Guarded by s_lock: requests from other tasks and the published snapshot. */
static uint32_t s_period_ms = POWER_DEFAULT_PERIOD_MS;
static battery_presence_t s_presence;
static bool s_inject;
static int32_t s_inject_mv;
static bool s_inject_chrg;
static bool s_inject_stdby;
static bool s_inputs_changed;
static power_battery_t s_snap;

/* pwr task only. */
static battery_filter_t s_filter;
static battery_t s_battery;
static uint16_t s_ring[EDGE_RING_SLOTS];
static size_t s_ring_pos;
static int s_raw;
static int s_adc_mv;
static int32_t s_sample_mv;
static int32_t s_mv;
static bool s_chrg_level;
static bool s_stdby_level;
static uint16_t s_edges_recent;
static uint16_t s_edges_10s;
static uint32_t s_edge_rate;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool lock(void)
{
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void IRAM_ATTR chrg_isr(void *arg)
{
    (void)arg;
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_edge_mux);
    const uint32_t n = ++s_edges;
    if (n == 1) {
        s_edge_first_us = now_us;
    }
    if (n >= EDGE_STORM_LIMIT && !s_edge_storm) {
        s_edge_storm = true;
        s_edge_storm_us = now_us;
        gpio_intr_disable(BOARD_PIN_PWR_CHRG);
    }
    portEXIT_CRITICAL_ISR(&s_edge_mux);
}

static esp_err_t read_adc(int *raw, int *adc_mv, int32_t *mv)
{
    ESP_RETURN_ON_FALSE(s_adc != NULL, ESP_ERR_INVALID_STATE, TAG, "adc");
    if (xSemaphoreTake(s_adc_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    int value = 0;
    for (int i = 0; i < ADC_DISCARD && err == ESP_OK; i++) {
        err = adc_oneshot_read(s_adc, BOARD_BAT_ADC_CHANNEL, &value);
        esp_rom_delay_us(ADC_DISCARD_DELAY_US);
    }
    int32_t sum = 0;
    for (int i = 0; i < ADC_AVERAGE && err == ESP_OK; i++) {
        err = adc_oneshot_read(s_adc, BOARD_BAT_ADC_CHANNEL, &value);
        sum += value;
        esp_rom_delay_us(ADC_SAMPLE_DELAY_US);
    }
    xSemaphoreGive(s_adc_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "adc read");

    const int avg = (int)(sum / ADC_AVERAGE);
    int pin_mv = 0;
    if (s_cali == NULL || adc_cali_raw_to_voltage(s_cali, avg, &pin_mv) != ESP_OK) {
        pin_mv = avg * ADC_UNCAL_FULL_MV / ADC_MAX_RAW;
    }
    *raw = avg;
    *adc_mv = pin_mv;
    *mv = (int32_t)((int64_t)pin_mv * BOARD_BAT_DIVIDER * CONFIG_DEVOTCHI_BATT_CAL_PERMILLE / 1000);
    return ESP_OK;
}

esp_err_t power_read_battery_mv_now(int32_t *mv)
{
    int raw;
    int adc_mv;
    return read_adc(&raw, &adc_mv, mv);
}

static uint32_t period_ms(void)
{
    uint32_t p = POWER_DEFAULT_PERIOD_MS;
    if (lock()) {
        p = s_period_ms;
        unlock();
    }
    return p;
}

/* Moves this period's edges into the ring and re-arms the interrupt after a storm. */
static void sample_edges(uint32_t period)
{
    portENTER_CRITICAL(&s_edge_mux);
    const uint32_t edges = s_edges;
    const bool storm = s_edge_storm;
    const int64_t burst_us = s_edge_storm_us - s_edge_first_us;
    s_edges = 0;
    s_edge_storm = false;
    portEXIT_CRITICAL(&s_edge_mux);
    if (storm) {
        gpio_intr_enable(BOARD_PIN_PWR_CHRG);
    }

    s_ring[s_ring_pos] = (uint16_t)(edges > UINT16_MAX ? UINT16_MAX : edges);
    s_ring_pos = (s_ring_pos + 1) % EDGE_RING_SLOTS;

    /* With a slow period fewer slots cover the same 10 s window. */
    size_t slots = EDGE_WINDOW_MS / period;
    slots = slots < 1 ? 1 : (slots > EDGE_RING_SLOTS ? EDGE_RING_SLOTS : slots);
    uint32_t sum = 0;
    for (size_t i = 1; i <= slots; i++) {
        sum += s_ring[(s_ring_pos + EDGE_RING_SLOTS - i) % EDGE_RING_SLOTS];
    }
    s_edges_recent = s_ring[(s_ring_pos + EDGE_RING_SLOTS - 1) % EDGE_RING_SLOTS];
    s_edges_10s = (uint16_t)(sum > UINT16_MAX ? UINT16_MAX : sum);
    /* After a storm the count is capped; the time the capped burst took still gives the rate. */
    if (storm && burst_us > 0) {
        s_edge_rate = (uint32_t)((EDGE_STORM_LIMIT - 1) * 1000000LL / burst_us);
    } else {
        s_edge_rate = s_edges_recent * 1000U / period;
    }
}

static void sample_adc(void)
{
    if (read_adc(&s_raw, &s_adc_mv, &s_sample_mv) != ESP_OK) {
        return;
    }
    bool injected = false;
    int32_t inject_mv = 0;
    if (lock()) {
        injected = s_inject;
        inject_mv = s_inject_mv;
        unlock();
    }
    s_mv = battery_filter_add(&s_filter, injected ? inject_mv : s_sample_mv);
    ESP_LOGD(TAG, "raw %d adc %d mV batt %ld mV filtered %ld mV", s_raw, s_adc_mv, (long)s_sample_mv, (long)s_mv);
}

static void evaluate(uint32_t now)
{
    if (!lock()) {
        return;
    }
    const bool injected = s_inject;
    const bool inputs_changed = s_inputs_changed;
    s_inputs_changed = false;
    const int32_t inject_mv = s_inject_mv;
    battery_input_t in = {
        .presence = s_presence,
        .now_ms = now,
    };
    if (injected) {
        in.chrg_low = s_inject_chrg;
        in.stdby_low = s_inject_stdby;
    } else {
        in.chrg_low = s_chrg_level || s_edges_recent > 0;
        in.stdby_low = s_stdby_level;
        in.chrg_edges_10s = s_edges_10s;
    }
    unlock();

    if (inputs_changed) {
        battery_filter_init(&s_filter);
        battery_reset_charge_memory(&s_battery);
        s_mv = battery_filter_add(&s_filter, injected ? inject_mv : s_sample_mv);
    }
    in.mv = s_mv;

    battery_output_t out;
    battery_update(&s_battery, &in, &out);

    bool notify = false;
    if (lock()) {
        const power_battery_t prev = s_snap;
        s_snap = (power_battery_t){
            .valid = true,
            .raw = s_raw,
            .adc_mv = s_adc_mv,
            .sample_mv = s_sample_mv,
            .mv = s_mv,
            .pct = out.pct,
            .state = out.state,
            .chrg = s_chrg_level,
            .stdby = s_stdby_level,
            .chrg_edges_10s = s_edges_10s,
            .chrg_edge_rate_hz = s_edge_rate,
            .ext_power = out.ext_power,
            .low = out.low,
            .critical = out.critical,
            .injected = injected,
            .presence = in.presence,
        };
        notify = !prev.valid || prev.state != out.state || prev.pct != out.pct ||
                 prev.ext_power != out.ext_power || prev.low != out.low || prev.critical != out.critical;
        unlock();
    }
    if (notify) {
        ESP_LOGD(TAG, "%s %d%% ext %d low %d critical %d", battery_state_name(out.state), out.pct, out.ext_power,
                 out.low, out.critical);
        if (s_cb != NULL) {
            s_cb(out.ext_power_changed);
        }
    }
}

static void power_task(void *arg)
{
    (void)arg;
    uint32_t next_tick = now_ms();
    uint32_t next_adc = next_tick;
    for (;;) {
        const uint32_t period = period_ms();
        uint32_t now = now_ms();
        if ((int32_t)(now - next_tick) >= 0) {
            sample_edges(period);
            s_chrg_level = gpio_get_level(BOARD_PIN_PWR_CHRG) == 0;
            s_stdby_level = gpio_get_level(BOARD_PIN_PWR_STDBY) == 0;
            if ((int32_t)(now - next_adc) >= 0) {
                sample_adc();
                next_adc = now + (period > ADC_PERIOD_MS ? period : ADC_PERIOD_MS);
            }
            next_tick += period;
            if ((int32_t)(now - next_tick) >= 0) {
                next_tick = now + period;
            }
        }
        evaluate(now);
        now = now_ms();
        const uint32_t wait = (int32_t)(next_tick - now) > 0 ? next_tick - now : 0;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
    }
}

static void wake_task(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

void power_get_battery(power_battery_t *out)
{
    *out = (power_battery_t){0};
    if (lock()) {
        *out = s_snap;
        unlock();
    }
}

void power_set_sample_period_ms(uint32_t period)
{
    if (period < POWER_DEFAULT_PERIOD_MS) {
        period = POWER_DEFAULT_PERIOD_MS;
    }
    if (lock()) {
        s_period_ms = period;
        unlock();
    }
}

void power_inject(int32_t mv, bool chrg, bool stdby)
{
    if (!lock()) {
        return;
    }
    s_inject = true;
    s_inject_mv = mv;
    s_inject_chrg = chrg;
    s_inject_stdby = stdby;
    s_inputs_changed = true;
    unlock();
    wake_task();
}

void power_inject_off(void)
{
    if (!lock()) {
        return;
    }
    s_inputs_changed = s_inject;
    s_inject = false;
    unlock();
    wake_task();
}

static battery_presence_t read_presence(void)
{
    const uint16_t v = storage_get_setting(STORAGE_SETTING_BATT);
    return v <= BATTERY_PRESENCE_NO ? (battery_presence_t)v : BATTERY_PRESENCE_AUTO;
}

void power_reload_settings(void)
{
    const battery_presence_t presence = read_presence();
    if (lock()) {
        s_presence = presence;
        unlock();
    }
    wake_task();
}

static esp_err_t init_adc(void)
{
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = BOARD_BAT_ADC_UNIT};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "adc unit");
    const adc_oneshot_chan_cfg_t chan = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BOARD_BAT_ADC_CHANNEL, &chan), TAG, "adc channel");

    const adc_cali_curve_fitting_config_t cali = {
        .unit_id = BOARD_BAT_ADC_UNIT,
        .chan = BOARD_BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "no ADC calibration in eFuse, using the nominal scale");
    }
    return ESP_OK;
}

static esp_err_t init_charger_pins(void)
{
    /* TP4056 CHRG and STDBY are open drain with no external pull-up. */
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_PIN_PWR_CHRG) | (1ULL << BOARD_PIN_PWR_STDBY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "charger pins");

    /*
     * A charger without a cell pulses CHRG faster than the 1 s level sampling
     * can see, so its edges are counted by interrupt. The service may already
     * exist (installed by the button library in power-save mode).
     */
    const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(BOARD_PIN_PWR_CHRG, GPIO_INTR_ANYEDGE), TAG, "chrg intr");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BOARD_PIN_PWR_CHRG, chrg_isr, NULL), TAG, "chrg isr");
    return gpio_intr_enable(BOARD_PIN_PWR_CHRG);
}

esp_err_t power_init(power_cb_t cb)
{
    s_cb = cb;
    s_lock = xSemaphoreCreateMutex();
    s_adc_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL && s_adc_lock != NULL, ESP_ERR_NO_MEM, TAG, "locks");
    s_presence = read_presence();
    battery_filter_init(&s_filter);
    battery_init(&s_battery, CONFIG_DEVOTCHI_BATT_LOW_MV, CONFIG_DEVOTCHI_BATT_CRIT_MV);
    ESP_RETURN_ON_ERROR(init_adc(), TAG, "adc");
    return init_charger_pins();
}

esp_err_t power_start(const char *task_name, uint32_t stack_size, uint32_t priority)
{
    const BaseType_t ok = xTaskCreate(power_task, task_name, stack_size, NULL, priority, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
