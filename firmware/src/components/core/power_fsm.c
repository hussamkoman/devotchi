#include "power_fsm.h"

#include <string.h>

/* Multipliers of the Screen timeout T (architecture 4.8). */
#define BATT_DIM_T  1u
#define BATT_OFF_T  2u
#define EXT_DIM_T   2u
#define EXT_OFF_T   10u

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
    /* The lowbatt card must be readable whatever the idle stage is. */
    if (out->show_lowbatt) {
        stage = POWER_STAGE_ACTIVE;
    }

    const bool off = stage == POWER_STAGE_OFF;
    out->stage = stage;
    out->backlight_pct = stage == POWER_STAGE_ACTIVE ? in->brightness_pct : stage == POWER_STAGE_DIM ? in->dim_pct : 0;
    out->panel_sleep = off;
    out->hold_cpu_lock = !off;
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
