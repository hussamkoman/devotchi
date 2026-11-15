#pragma once

#include "battery.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** Charger pins, edge window and battery state every period; the ADC every max(period, 2 s). */
#define POWER_DEFAULT_PERIOD_MS  1000

typedef struct {
    /** False until the pwr task finished its first evaluation. */
    bool valid;
    /** Averaged ADC counts of the last reading. */
    int raw;
    /** Calibrated voltage at the ADC pin. */
    int adc_mv;
    /** Battery voltage of the last reading (divider and calibration factor applied). */
    int32_t sample_mv;
    /** Filtered battery voltage, or the injected one. */
    int32_t mv;
    int pct;
    battery_state_t state;
    /** True while the pin is low (asserted) at the last level sample. */
    bool chrg;
    bool stdby;
    uint16_t chrg_edges_10s;
    /** CHRG edges per second over the last sample period; measured from the burst when the storm guard tripped. */
    uint32_t chrg_edge_rate_hz;
    bool ext_power;
    bool low;
    bool critical;
    bool injected;
    battery_presence_t presence;
} power_battery_t;

/** Runs in the pwr task when state, pct, external power, low or critical changed; must only post. */
typedef void (*power_cb_t)(bool ext_power_changed);

/**
 * Sets up the battery ADC, the charger pins and the CHRG edge interrupt, and
 * reads the presence setting. Must run after buttons_init (GPIO ISR service).
 */
esp_err_t power_init(power_cb_t cb);

esp_err_t power_start(const char *task_name, uint32_t stack_size, uint32_t priority);

/** One battery reading (4 discarded + 32 averaged ADC reads); usable before power_start. */
esp_err_t power_read_battery_mv_now(int32_t *mv);

/** Copies the latest battery evaluation; safe from any task. */
void power_get_battery(power_battery_t *out);

/** Changes the sampling period (at least POWER_DEFAULT_PERIOD_MS). */
void power_set_sample_period_ms(uint32_t period_ms);

/**
 * Replaces the inputs of the battery evaluation (CHRG/STDBY asserted flags,
 * no edges) until power_inject_off; resets the filter.
 */
void power_inject(int32_t mv, bool chrg, bool stdby);
void power_inject_off(void);

/** Re-reads the battery presence setting. */
void power_reload_settings(void);

/* ---- power management and sleep (power_sleep.c) ---- */

typedef struct {
    /** This boot is a wake from deep sleep. */
    bool deep_sleep;
    /** esp_sleep_get_wakeup_causes() bits. */
    uint32_t causes;
    bool ext1;
    bool timer;
    /** GPIO mask of the ext1 pins that were low. */
    uint64_t ext1_pins;
} power_wake_t;

/**
 * In app_main before any driver configures a pad: reads the wake cause and,
 * after a deep sleep, releases the pad holds.
 */
void power_boot_early(void);

void power_get_wake(power_wake_t *out);

/** "approve", "reject", "action", or "" when no button woke the device. */
const char *power_wake_button_name(const power_wake_t *w);

/** RTC memory flag: the last deep sleep was a battery-protection sleep. */
bool power_slept_for_battery(void);
void power_set_slept_for_battery(bool on);

typedef struct {
    bool configured;
    bool cpu_lock;
    bool no_light_sleep_lock;
    uint32_t cpu_mhz;
} power_pm_state_t;

/**
 * Creates the `cpu` (ESP_PM_CPU_FREQ_MAX) and `no_ls` (ESP_PM_NO_LIGHT_SLEEP)
 * locks, takes both and then configures DFS from 40 MHz to the default CPU
 * frequency, with automatic light sleep when CONFIG_DEVOTCHI_PWR_LIGHT_SLEEP.
 */
esp_err_t power_pm_init(void);

/** One caller (the app task): holds or releases the two locks. */
void power_pm_set(bool hold_cpu, bool allow_light_sleep);
void power_pm_get(power_pm_state_t *out);

/** A USB host has the Serial-JTAG port open (start-of-frame packets seen). */
bool power_usb_host(void);

/** CHRG and STDBY now, true = asserted (low); usable after power_init. */
void power_read_charger_now(bool *chrg, bool *stdby);

/**
 * Drives backlight, motor and LED low and the display RST and CS high, holds
 * them, arms the ext1 wake on the three buttons (any low) plus a timer wake
 * when timer_s > 0, and enters deep sleep. Restarts if no wake source could be
 * armed; does not return.
 */
void power_deep_sleep(uint32_t timer_s) __attribute__((noreturn));
