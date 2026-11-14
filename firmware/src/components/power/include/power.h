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
