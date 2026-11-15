#include "power.h"

#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_clk_tree.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"

static const char *TAG = "sleep";

#define PM_MIN_FREQ_MHZ  40
#define RTC_MAGIC        0x44565053u

/* Survive deep sleep; the magic tells a wake apart from RTC memory left over by another reset. */
static RTC_DATA_ATTR uint32_t s_rtc_magic;
static RTC_DATA_ATTR bool s_rtc_slept_for_battery;

static esp_pm_lock_handle_t s_cpu_lock;
static esp_pm_lock_handle_t s_no_ls_lock;
static bool s_cpu_held;
static bool s_no_ls_held;
static bool s_pm_configured;
static power_wake_t s_wake;

typedef struct {
    gpio_num_t pin;
    uint32_t level;
} sleep_pad_t;

/* Inactive levels: backlight, motor and LED off; panel out of reset and not selected. */
static const sleep_pad_t SLEEP_PADS[] = {
    {BOARD_PIN_DISP_BL, 0},
    {BOARD_PIN_MOTOR, 0},
    {BOARD_PIN_LED, 0},
    {BOARD_PIN_DISP_RST, 1},
    {BOARD_PIN_DISP_CS, 1},
};

#define SLEEP_PAD_COUNT (sizeof(SLEEP_PADS) / sizeof(SLEEP_PADS[0]))

static const gpio_num_t WAKE_PINS[] = {BOARD_PIN_BTN_APPROVE, BOARD_PIN_BTN_REJECT, BOARD_PIN_BTN_ACTION};

#define WAKE_PIN_COUNT (sizeof(WAKE_PINS) / sizeof(WAKE_PINS[0]))

void power_boot_early(void)
{
    s_wake.deep_sleep = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    s_wake.causes = esp_sleep_get_wakeup_causes();
    s_wake.ext1 = (s_wake.causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) != 0;
    s_wake.timer = (s_wake.causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) != 0;
    if (s_wake.ext1) {
        s_wake.ext1_pins = esp_sleep_get_ext1_wakeup_status();
    }
    if (!s_wake.deep_sleep || s_rtc_magic != RTC_MAGIC) {
        s_rtc_slept_for_battery = false;
    }
    s_rtc_magic = RTC_MAGIC;
    if (!s_wake.deep_sleep) {
        return;
    }
    /* Each pad drives its held level before the hold ends, so nothing floats until its driver takes over. */
    for (size_t i = 0; i < SLEEP_PAD_COUNT; i++) {
        gpio_set_level(SLEEP_PADS[i].pin, SLEEP_PADS[i].level);
        gpio_set_direction(SLEEP_PADS[i].pin, GPIO_MODE_OUTPUT);
        gpio_hold_dis(SLEEP_PADS[i].pin);
    }
    gpio_deep_sleep_hold_dis();
    ESP_LOGI(TAG, "woke from deep sleep: causes 0x%lx, ext1 pins 0x%llx%s", (unsigned long)s_wake.causes,
             (unsigned long long)s_wake.ext1_pins, s_wake.timer ? ", timer" : "");
}

void power_get_wake(power_wake_t *out)
{
    *out = s_wake;
}

const char *power_wake_button_name(const power_wake_t *w)
{
    if (!w->ext1) {
        return "";
    }
    if (w->ext1_pins & BIT64(BOARD_PIN_BTN_APPROVE)) {
        return "approve";
    }
    if (w->ext1_pins & BIT64(BOARD_PIN_BTN_REJECT)) {
        return "reject";
    }
    if (w->ext1_pins & BIT64(BOARD_PIN_BTN_ACTION)) {
        return "action";
    }
    return "";
}

bool power_slept_for_battery(void)
{
    return s_rtc_slept_for_battery;
}

void power_set_slept_for_battery(bool on)
{
    s_rtc_slept_for_battery = on;
}

esp_err_t power_pm_init(void)
{
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "cpu", &s_cpu_lock), TAG, "cpu lock");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_ls", &s_no_ls_lock), TAG, "no_ls lock");
    /* Both are taken before DFS starts, so the CPU never drops below full speed during boot. */
    ESP_RETURN_ON_ERROR(esp_pm_lock_acquire(s_cpu_lock), TAG, "cpu acquire");
    ESP_RETURN_ON_ERROR(esp_pm_lock_acquire(s_no_ls_lock), TAG, "no_ls acquire");
    s_cpu_held = true;
    s_no_ls_held = true;
    const esp_pm_config_t cfg = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = PM_MIN_FREQ_MHZ,
#if CONFIG_DEVOTCHI_PWR_LIGHT_SLEEP
        .light_sleep_enable = true,
#else
        .light_sleep_enable = false,
#endif
    };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&cfg), TAG, "pm configure");
    s_pm_configured = true;
    return ESP_OK;
}

static void set_lock(esp_pm_lock_handle_t lock, bool *held, bool want)
{
    if (lock == NULL || *held == want) {
        return;
    }
    const esp_err_t err = want ? esp_pm_lock_acquire(lock) : esp_pm_lock_release(lock);
    if (err == ESP_OK) {
        *held = want;
    } else {
        ESP_LOGE(TAG, "pm lock %s: %s", want ? "acquire" : "release", esp_err_to_name(err));
    }
}

void power_pm_set(bool hold_cpu, bool allow_light_sleep)
{
    set_lock(s_cpu_lock, &s_cpu_held, hold_cpu);
    set_lock(s_no_ls_lock, &s_no_ls_held, !allow_light_sleep);
}

void power_pm_get(power_pm_state_t *out)
{
    uint32_t hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
    *out = (power_pm_state_t){
        .configured = s_pm_configured,
        .cpu_lock = s_cpu_held,
        .no_light_sleep_lock = s_no_ls_held,
        .cpu_mhz = hz / 1000000u,
    };
}

bool power_usb_host(void)
{
    return usb_serial_jtag_is_connected();
}

void power_read_charger_now(bool *chrg, bool *stdby)
{
    *chrg = gpio_get_level(BOARD_PIN_PWR_CHRG) == 0;
    *stdby = gpio_get_level(BOARD_PIN_PWR_STDBY) == 0;
}

void power_deep_sleep(uint32_t timer_s)
{
    for (size_t i = 0; i < SLEEP_PAD_COUNT; i++) {
        gpio_set_level(SLEEP_PADS[i].pin, SLEEP_PADS[i].level);
        gpio_set_direction(SLEEP_PADS[i].pin, GPIO_MODE_OUTPUT);
        gpio_hold_en(SLEEP_PADS[i].pin);
    }
    gpio_deep_sleep_hold_en();

    uint64_t mask = 0;
    for (size_t i = 0; i < WAKE_PIN_COUNT; i++) {
        mask |= BIT64(WAKE_PINS[i]);
        /* The buttons have external 10k pull-ups; internal ones would only add leakage. */
        rtc_gpio_pullup_dis(WAKE_PINS[i]);
        rtc_gpio_pulldown_dis(WAKE_PINS[i]);
    }
    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err == ESP_OK && timer_s > 0) {
        err = esp_sleep_enable_timer_wakeup((uint64_t)timer_s * 1000000ULL);
    }
    if (err != ESP_OK) {
        /* Without a wake source the device could only be revived by the reset button. */
        ESP_LOGE(TAG, "wake source: %s; restarting instead", esp_err_to_name(err));
        esp_restart();
    }
    ESP_LOGI(TAG, "deep sleep (timer %lu s)", (unsigned long)timer_s);
    esp_deep_sleep_start();
}
