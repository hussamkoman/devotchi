#include "power_fsm.h"

#include <string.h>

/* Multipliers of the Screen timeout T (architecture 4.8). */
#define BATT_DIM_T  1u
#define BATT_OFF_T  2u
#define EXT_DIM_T   2u
#define EXT_OFF_T   10u

static const char *const CLOCK_MODE_NAMES[POWER_CLOCK_MODE_COUNT] = {"resting", "always_usb", "never"};

static const char *const STAGE_NAMES[POWER_STAGE_COUNT] = {"active", "dim", "off"};

static const char *const BLOCK_NAMES[POWER_BLOCK_COUNT] = {
    "stage", "pending", "never", "idle", "usb_host", "provisioning", "boot_grace",
};

static bool newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

/* Time since then; 0 for a timestamp that lies ahead of now. */
static uint32_t since(uint32_t now, uint32_t then)
{
    return (int32_t)(now - then) > 0 ? now - then : 0;
}

void power_fsm_init(power_fsm_t *f)
{
    memset(f, 0, sizeof(*f));
}

void power_fsm_force_stage(power_fsm_t *f, power_stage_t stage, uint32_t last_user_ms)
{
    f->forced = stage < POWER_STAGE_COUNT;
    f->forced_stage = stage;
    f->seen_user_ms = last_user_ms;
    f->started = true;
}

static power_stage_t idle_stage(const power_fsm_input_t *in, power_fsm_output_t *out)
{
    const uint32_t t_ms = (uint32_t)in->screen_timeout_s * 1000u;
    out->dim_ms = t_ms * (in->ext_power ? EXT_DIM_T : BATT_DIM_T);
    out->off_ms = t_ms * (in->ext_power ? EXT_OFF_T : BATT_OFF_T);
    if (t_ms == 0) {
        return POWER_STAGE_ACTIVE;
    }
    if (out->idle_ms >= out->off_ms) {
        return POWER_STAGE_OFF;
    }
    return out->idle_ms >= out->dim_ms ? POWER_STAGE_DIM : POWER_STAGE_ACTIVE;
}

static power_stage_t screen_stage(power_fsm_t *f, const power_fsm_input_t *in, power_fsm_output_t *out,
                                  uint32_t last_user_ms)
{
    const power_stage_t computed = idle_stage(in, out);
    if (in->card_visible) {
        f->stage = POWER_STAGE_ACTIVE;
        f->forced = false;
        f->started = true;
        f->seen_user_ms = last_user_ms;
        return POWER_STAGE_ACTIVE;
    }
    /* Only user activity brightens the screen; a longer timeout set mid-idle does not. */
    if (!f->started || newer(last_user_ms, f->seen_user_ms)) {
        f->stage = computed;
        f->forced = false;
    } else if (computed > f->stage) {
        f->stage = computed;
    }
    f->started = true;
    f->seen_user_ms = last_user_ms;

    power_stage_t stage = f->forced ? f->forced_stage : f->stage;
    if (in->pending_count > 0 && stage > POWER_STAGE_DIM) {
        stage = POWER_STAGE_DIM;
    }
    return stage;
}

static uint32_t idle_blockers(const power_fsm_input_t *in, power_fsm_output_t *out, power_stage_t stage,
                              bool usb_blocks, bool in_grace)
{
    uint32_t b = 0;
    if (stage != POWER_STAGE_OFF) {
        b |= POWER_BLOCK_STAGE;
    }
    if (in->pending_count > 0) {
        b |= POWER_BLOCK_PENDING;
    }
    if (out->deep_ms == 0) {
        b |= POWER_BLOCK_NEVER;
    } else if (out->agent_idle_ms < out->deep_ms) {
        b |= POWER_BLOCK_IDLE;
    }
    if (usb_blocks) {
        b |= POWER_BLOCK_USB_HOST;
    }
    if (in->provisioning) {
        b |= POWER_BLOCK_PROVISIONING;
    }
    if (in_grace) {
        b |= POWER_BLOCK_BOOT_GRACE;
    }
    return b;
}

/* Battery protection: true when a battery-protection sleep is due now. */
static bool battery_sleep(power_fsm_t *f, const power_fsm_input_t *in, power_fsm_output_t *out, bool usb_blocks,
                          bool in_grace)
{
    const bool on_battery = in->batt_present && !in->ext_power;

    if (on_battery && in->batt_low) {
        out->low_toast = !f->low_warned;
        f->low_warned = true;
    } else {
        f->low_warned = false;
    }

    bool due = false;
    if (in->wake_reason_battery && !f->wake_done) {
        if (on_battery && in->batt_critical && !usb_blocks) {
            if (!f->wake_timing) {
                f->wake_timing = true;
                f->wake_since_ms = in->now_ms;
            }
            out->show_lowbatt = true;
            /* No boot grace here: a nearly empty cell must not be held awake on every button press. */
            due = since(in->now_ms, f->wake_since_ms) >= POWER_FSM_WAKE_LOWBATT_MS;
        } else {
            f->wake_done = true;
        }
    }
    out->wake_check_pending = in->wake_reason_battery && !f->wake_done;

    if (on_battery && in->batt_critical) {
        if (!f->crit_timing) {
            f->crit_timing = true;
            f->crit_since_ms = in->now_ms;
        }
        out->show_lowbatt = true;
        if (since(in->now_ms, f->crit_since_ms) >= POWER_FSM_LOWBATT_MS && !usb_blocks && !in_grace) {
            due = true;
        }
    } else {
        f->crit_timing = false;
    }
    return due;
}

/* Toasts at 15 % and 8 % on battery; a jump straight below 8 % shows only the second. */
static uint8_t low_pct_toast(power_fsm_t *f, const power_fsm_input_t *in)
{
    if (!in->batt_present || in->ext_power) {
        f->low_pct_level = 0;
        return 0;
    }
    if (in->batt_pct <= POWER_FSM_LOW_PCT_2 && f->low_pct_level < 2) {
        f->low_pct_level = 2;
        return POWER_FSM_LOW_PCT_2;
    }
    if (in->batt_pct <= POWER_FSM_LOW_PCT_1 && f->low_pct_level < 1) {
        f->low_pct_level = 1;
        return POWER_FSM_LOW_PCT_1;
    }
    return 0;
}

/*
 * The resting face (ux-flows 5): the clock replaces home in dim when nothing
 * waits for the user; Always on USB power keeps it lit instead of off.
 */
static power_stage_t resting_face(const power_fsm_input_t *in, power_fsm_output_t *out, power_stage_t stage)
{
    const bool eligible = in->clock_mode != POWER_CLOCK_NEVER && in->agent_idle && in->pending_count == 0 &&
                          !in->card_visible;
    if (!eligible) {
        return stage;
    }
    if (stage == POWER_STAGE_OFF && in->clock_mode == POWER_CLOCK_ALWAYS_USB && in->ext_power) {
        stage = POWER_STAGE_DIM;
    }
    out->show_clock = stage == POWER_STAGE_DIM ||
                      (stage == POWER_STAGE_ACTIVE && in->screen_timeout_s == 0 &&
                       out->idle_ms >= POWER_FSM_NEVER_CLOCK_MS);
    return stage;
}

static uint8_t backlight(const power_fsm_input_t *in, const power_fsm_output_t *out)
{
    if (out->stage == POWER_STAGE_OFF) {
        return 0;
    }
    if (out->show_clock) {
        if (in->night) {
            return POWER_FSM_CLOCK_NIGHT_PCT;
        }
        return in->ext_power ? in->clock_brightness_pct : POWER_FSM_CLOCK_BATT_PCT;
    }
    return out->stage == POWER_STAGE_ACTIVE || out->sleep_warning ? in->brightness_pct : in->dim_pct;
}

void power_fsm_update(power_fsm_t *f, const power_fsm_input_t *in, power_fsm_output_t *out)
{
    memset(out, 0, sizeof(*out));
    /* A card that closes counts as user activity at that moment: the idle timers restart from it. */
    if (in->card_visible) {
        f->card_shown = true;
    } else if (f->card_shown) {
        f->card_shown = false;
        f->card_closed = true;
        f->card_closed_ms = in->now_ms;
    }
    uint32_t last_user = in->last_user_ms;
    if (f->card_closed && newer(f->card_closed_ms, last_user)) {
        last_user = f->card_closed_ms;
    }
    out->idle_ms = in->card_visible ? 0 : since(in->now_ms, last_user);
    const uint32_t last_any = newer(in->last_agent_ms, last_user) ? in->last_agent_ms : last_user;
    out->agent_idle_ms = since(in->now_ms, last_any);
    out->deep_ms = in->ext_power ? in->usb_deep_timeout_ms : in->deep_timeout_ms;

    const bool usb_blocks = in->usb_host && !in->dry_run;
    const bool in_grace = since(in->now_ms, in->boot_ms) < (uint32_t)in->boot_grace_s * 1000u;

    power_stage_t stage = screen_stage(f, in, out, last_user);
    out->blockers = idle_blockers(in, out, stage, usb_blocks, in_grace);

    const bool battery_due = battery_sleep(f, in, out, usb_blocks, in_grace);
    if (battery_due) {
        out->enter_deep_sleep = true;
        out->deep_reason = POWER_DEEP_BATTERY;
    } else if (out->blockers == 0) {
        out->enter_deep_sleep = true;
        out->deep_reason = POWER_DEEP_IDLE;
    }
    out->low_toast_pct = low_pct_toast(f, in);
    /* The only thing missing for an idle deep sleep is the last ten seconds of the timeout. */
    if (!out->enter_deep_sleep && out->blockers == POWER_BLOCK_IDLE && out->deep_ms - out->agent_idle_ms <=
                                                                           POWER_FSM_SLEEP_WARN_MS) {
        out->sleep_warning = true;
        out->sleep_in_ms = out->deep_ms - out->agent_idle_ms;
    }
    /* The lowbatt card must be readable whatever the idle stage is. */
    if (out->show_lowbatt) {
        stage = POWER_STAGE_ACTIVE;
    } else if (out->sleep_warning) {
        /* Lit but not active, so the press that cancels the sleep is a wake press. */
        stage = POWER_STAGE_DIM;
    } else {
        stage = resting_face(in, out, stage);
    }

    const bool off = stage == POWER_STAGE_OFF;
    out->stage = stage;
    out->backlight_pct = backlight(in, out);
    out->panel_sleep = off;
    /* The clock face animates at 10 fps and changes digits once a minute: it can run with the CPU unlocked. */
    out->hold_cpu_lock = !off && !out->show_clock;
    out->wifi_ps = off && !in->ext_power ? POWER_WIFI_PS_MAX : POWER_WIFI_PS_MIN;
    out->allow_light_sleep = off && !in->ext_power && !in->usb_host && in->light_sleep_enabled && !in->provisioning &&
                             !in_grace;
}

const char *power_stage_name(power_stage_t stage)
{
    return stage < POWER_STAGE_COUNT ? STAGE_NAMES[stage] : "";
}

bool power_stage_parse(const char *name, power_stage_t *out)
{
    for (int i = 0; i < POWER_STAGE_COUNT; i++) {
        if (strcmp(name, STAGE_NAMES[i]) == 0) {
            *out = (power_stage_t)i;
            return true;
        }
    }
    return false;
}

const char *power_block_name(uint32_t bit)
{
    for (int i = 0; i < POWER_BLOCK_COUNT; i++) {
        if (bit == (1u << i)) {
            return BLOCK_NAMES[i];
        }
    }
    return "";
}

const char *power_deep_reason_name(power_deep_reason_t reason)
{
    return reason == POWER_DEEP_BATTERY ? "battery" : "idle";
}

const char *power_clock_mode_name(power_clock_mode_t mode)
{
    return mode < POWER_CLOCK_MODE_COUNT ? CLOCK_MODE_NAMES[mode] : "";
}
