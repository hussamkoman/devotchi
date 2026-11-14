#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BATTERY_MEDIAN_N         5
/** External power counts as present this long after CHRG or STDBY was last seen low. */
#define BATTERY_EXT_HOLD_MS      2000
/** CHRG edges in the 10 s window that mark a charger without a cell (presence auto). */
#define BATTERY_ABSENT_EDGES     3
#define BATTERY_FULL_MV          4150
#define BATTERY_FULL_HOLD_MS     (10U * 60U * 1000U)
#define BATTERY_CRIT_HOLD_MS     30000
/** Charging raises the terminal voltage; the table is for open-circuit voltage. */
#define BATTERY_CHARGE_OFFSET_MV 100

/** Values are the NVS s_batt encoding. */
typedef enum {
    BATTERY_PRESENCE_AUTO = 0,
    BATTERY_PRESENCE_YES = 1,
    BATTERY_PRESENCE_NO = 2,
} battery_presence_t;

typedef enum {
    BATTERY_STATE_ABSENT,
    BATTERY_STATE_FULL,
    BATTERY_STATE_CHARGING,
    BATTERY_STATE_DISCHARGING,
} battery_state_t;

/** Sliding median of the last BATTERY_MEDIAN_N samples, then new = old + (median - old) / 4. */
typedef struct {
    int32_t window[BATTERY_MEDIAN_N];
    uint8_t count;
    uint8_t next;
    bool primed;
    /** Filter state in 1/16 mV so the integer filter settles on the input. */
    int32_t value_x16;
} battery_filter_t;

void battery_filter_init(battery_filter_t *f);

/** Adds one raw sample and returns the filtered millivolts. */
int32_t battery_filter_add(battery_filter_t *f, int32_t mv);

/** Median of exactly five samples (the boot burst). */
int32_t battery_median5(const int32_t samples[BATTERY_MEDIAN_N]);

/** LiPo open-circuit table with linear interpolation, clamped to 0..100. */
int battery_pct_from_mv(int32_t mv);

typedef struct {
    /** Filtered battery voltage. */
    int32_t mv;
    /** CHRG low now, or an edge on CHRG since the previous update. */
    bool chrg_low;
    bool stdby_low;
    uint16_t chrg_edges_10s;
    battery_presence_t presence;
    uint32_t now_ms;
} battery_input_t;

typedef struct {
    battery_state_t state;
    /** -1 when absent. */
    int pct;
    /** Debounced: CHRG or STDBY seen low within BATTERY_EXT_HOLD_MS. */
    bool ext_power;
    /** True only on the update where the debounced ext_power flipped. */
    bool ext_power_changed;
    bool low;
    bool critical;
} battery_output_t;

typedef struct {
    int32_t low_mv;
    int32_t crit_mv;
    bool ext_seen;
    uint32_t ext_last_ms;
    bool ext_power;
    bool full_timing;
    uint32_t full_since_ms;
    bool crit_timing;
    uint32_t crit_since_ms;
    bool have_state;
    battery_state_t state;
    int pct;
} battery_t;

void battery_init(battery_t *b, int32_t low_mv, int32_t crit_mv);

void battery_update(battery_t *b, const battery_input_t *in, battery_output_t *out);

/**
 * Forgets the percent memory and the full and critical timers but keeps the
 * external-power debounce, for when the inputs jump (injection on or off).
 */
void battery_reset_charge_memory(battery_t *b);

const char *battery_state_name(battery_state_t state);
const char *battery_presence_name(battery_presence_t presence);
