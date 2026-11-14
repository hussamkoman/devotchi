#include "battery.h"

#include <stddef.h>

typedef struct {
    int32_t mv;
    int pct;
} curve_point_t;

/* Single-cell LiPo, open-circuit voltage, descending. */
static const curve_point_t CURVE[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3950, 75}, {3900, 68}, {3850, 60},
    {3800, 50},  {3750, 38}, {3700, 25}, {3600, 12}, {3500, 5},  {3300, 0},
};

#define CURVE_LEN (sizeof(CURVE) / sizeof(CURVE[0]))

void battery_filter_init(battery_filter_t *f)
{
    *f = (battery_filter_t){0};
}

static void sort_small(int32_t *v, size_t n)
{
    for (size_t i = 1; i < n; i++) {
        const int32_t x = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
}

int32_t battery_median5(const int32_t samples[BATTERY_MEDIAN_N])
{
    int32_t v[BATTERY_MEDIAN_N];
    for (size_t i = 0; i < BATTERY_MEDIAN_N; i++) {
        v[i] = samples[i];
    }
    sort_small(v, BATTERY_MEDIAN_N);
    return v[BATTERY_MEDIAN_N / 2];
}

int32_t battery_filter_add(battery_filter_t *f, int32_t mv)
{
    f->window[f->next] = mv;
    f->next = (uint8_t)((f->next + 1) % BATTERY_MEDIAN_N);
    if (f->count < BATTERY_MEDIAN_N) {
        f->count++;
    }

    int32_t v[BATTERY_MEDIAN_N];
    for (size_t i = 0; i < f->count; i++) {
        v[i] = f->window[i];
    }
    sort_small(v, f->count);
    const int32_t median_x16 = v[(f->count - 1) / 2] * 16;

    if (!f->primed) {
        f->value_x16 = median_x16;
        f->primed = true;
    } else {
        f->value_x16 += (median_x16 - f->value_x16) / 4;
    }
    return (f->value_x16 + 8) / 16;
}

int battery_pct_from_mv(int32_t mv)
{
    if (mv >= CURVE[0].mv) {
        return CURVE[0].pct;
    }
    for (size_t i = 1; i < CURVE_LEN; i++) {
        if (mv >= CURVE[i].mv) {
            const curve_point_t *hi = &CURVE[i - 1];
            const curve_point_t *lo = &CURVE[i];
            return lo->pct + (int)((mv - lo->mv) * (hi->pct - lo->pct) / (hi->mv - lo->mv));
        }
    }
    return 0;
}

void battery_init(battery_t *b, int32_t low_mv, int32_t crit_mv)
{
    *b = (battery_t){.low_mv = low_mv, .crit_mv = crit_mv, .pct = -1};
}

static bool held_for(bool timing, uint32_t since_ms, uint32_t now_ms, uint32_t hold_ms)
{
    return timing && (uint32_t)(now_ms - since_ms) >= hold_ms;
}

void battery_update(battery_t *b, const battery_input_t *in, battery_output_t *out)
{
    const uint32_t now = in->now_ms;

    /* A charger without a cell pulses CHRG; holding the last sighting keeps the pulses from looking like unplugging. */
    const bool ext_now = in->chrg_low || in->stdby_low;
    if (ext_now) {
        b->ext_seen = true;
        b->ext_last_ms = now;
    }
    const bool ext = ext_now || (b->ext_seen && (uint32_t)(now - b->ext_last_ms) <= BATTERY_EXT_HOLD_MS);
    const bool ext_changed = ext != b->ext_power;
    b->ext_power = ext;

    if (in->chrg_low && in->mv >= BATTERY_FULL_MV) {
        if (!b->full_timing) {
            b->full_timing = true;
            b->full_since_ms = now;
        }
    } else {
        b->full_timing = false;
    }

    battery_state_t state;
    const bool absent = in->presence == BATTERY_PRESENCE_NO ||
                        (in->presence == BATTERY_PRESENCE_AUTO && ext && in->chrg_edges_10s >= BATTERY_ABSENT_EDGES);
    if (absent) {
        state = BATTERY_STATE_ABSENT;
    } else if (ext && (in->stdby_low || held_for(b->full_timing, b->full_since_ms, now, BATTERY_FULL_HOLD_MS))) {
        state = BATTERY_STATE_FULL;
    } else if (ext) {
        state = BATTERY_STATE_CHARGING;
    } else {
        state = BATTERY_STATE_DISCHARGING;
    }

    if (!b->have_state || state != b->state) {
        b->have_state = true;
        b->state = state;
        b->pct = -1;
    }

    int pct = -1;
    switch (state) {
    case BATTERY_STATE_ABSENT:
        break;
    case BATTERY_STATE_FULL:
        pct = 100;
        break;
    case BATTERY_STATE_CHARGING:
        pct = battery_pct_from_mv(in->mv - BATTERY_CHARGE_OFFSET_MV);
        if (pct > 99) {
            pct = 99;
        }
        if (b->pct >= 0 && pct < b->pct) {
            pct = b->pct;
        }
        break;
    case BATTERY_STATE_DISCHARGING:
        pct = battery_pct_from_mv(in->mv);
        if (b->pct >= 0 && pct > b->pct) {
            pct = b->pct;
        }
        break;
    }
    b->pct = pct;

    const bool discharging = state == BATTERY_STATE_DISCHARGING;
    if (discharging && in->mv < b->crit_mv) {
        if (!b->crit_timing) {
            b->crit_timing = true;
            b->crit_since_ms = now;
        }
    } else {
        b->crit_timing = false;
    }

    *out = (battery_output_t){
        .state = state,
        .pct = pct,
        .ext_power = ext,
        .ext_power_changed = ext_changed,
        .low = discharging && in->mv < b->low_mv,
        .critical = held_for(b->crit_timing, b->crit_since_ms, now, BATTERY_CRIT_HOLD_MS),
    };
}

void battery_reset_charge_memory(battery_t *b)
{
    b->full_timing = false;
    b->crit_timing = false;
    b->have_state = false;
    b->pct = -1;
}

const char *battery_state_name(battery_state_t state)
{
    switch (state) {
    case BATTERY_STATE_ABSENT:
        return "absent";
    case BATTERY_STATE_FULL:
        return "full";
    case BATTERY_STATE_CHARGING:
        return "charging";
    case BATTERY_STATE_DISCHARGING:
        return "discharging";
    }
    return "";
}

const char *battery_presence_name(battery_presence_t presence)
{
    switch (presence) {
    case BATTERY_PRESENCE_AUTO:
        return "auto";
    case BATTERY_PRESENCE_YES:
        return "yes";
    case BATTERY_PRESENCE_NO:
        return "no";
    }
    return "";
}
