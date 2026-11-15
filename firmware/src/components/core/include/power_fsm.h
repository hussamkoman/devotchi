#pragma once

#include <stdbool.h>
#include <stdint.h>

/** The lowbatt screen shows this long before a battery-protection sleep. */
#define POWER_FSM_LOWBATT_MS       5000
/** The same, after a wake from a battery-protection sleep that still measures critical. */
#define POWER_FSM_WAKE_LOWBATT_MS  3000

typedef enum {
    POWER_STAGE_ACTIVE,
    POWER_STAGE_DIM,
    POWER_STAGE_OFF,
    POWER_STAGE_COUNT,
} power_stage_t;

/** Settings > Clock > Show clock (`s_clock`). */
typedef enum {
    POWER_CLOCK_RESTING,
    POWER_CLOCK_ALWAYS_USB,
    POWER_CLOCK_NEVER,
    POWER_CLOCK_MODE_COUNT,
} power_clock_mode_t;

/** Clock backlight on battery by day, and at night on any power (ux-flows 5). */
#define POWER_FSM_CLOCK_BATT_PCT   10
#define POWER_FSM_CLOCK_NIGHT_PCT  5
/** With Screen timeout = Never the clock replaces home after this much idle time. */
#define POWER_FSM_NEVER_CLOCK_MS   120000u
/** The going-to-sleep screen (91) shows this long before an idle deep sleep. */
#define POWER_FSM_SLEEP_WARN_MS    10000u
/** Low-battery toasts (90), once each per discharge. */
#define POWER_FSM_LOW_PCT_1        15
#define POWER_FSM_LOW_PCT_2        8

typedef enum {
    POWER_WIFI_PS_MIN,
    POWER_WIFI_PS_MAX,
} power_wifi_ps_t;

typedef enum {
    POWER_DEEP_IDLE,
    POWER_DEEP_BATTERY,
} power_deep_reason_t;

/** What keeps the device out of an idle deep sleep; bits of power_fsm_output_t.blockers. */
typedef enum {
    POWER_BLOCK_STAGE = 1u << 0,
    POWER_BLOCK_PENDING = 1u << 1,
    /** The applicable deep-sleep timeout is 0. */
    POWER_BLOCK_NEVER = 1u << 2,
    /** The deep-sleep timeout has not run out yet. */
    POWER_BLOCK_IDLE = 1u << 3,
    POWER_BLOCK_USB_HOST = 1u << 4,
    POWER_BLOCK_PROVISIONING = 1u << 5,
    POWER_BLOCK_BOOT_GRACE = 1u << 6,
} power_block_t;

#define POWER_BLOCK_COUNT 7

typedef struct {
    uint32_t now_ms;
    uint32_t boot_ms;
    uint32_t last_user_ms;
    uint32_t last_agent_ms;
    uint8_t pending_count;
    /**
     * A request card or one of its menus is on screen: the stage is held at
     * active (ux-flows 7); when it closes the idle timers start again.
     */
    bool card_visible;
    /** The debounced value from battery_update. */
    bool ext_power;
    bool usb_host;
    bool provisioning;
    bool batt_present;
    bool batt_low;
    bool batt_critical;
    /** This boot is a wake from a battery-protection sleep. */
    bool wake_reason_battery;
    /** The Screen timeout setting (T); 0 keeps the screen on. */
    uint16_t screen_timeout_s;
    /** On battery; 0 = never. */
    uint32_t deep_timeout_ms;
    /** On external power; 0 = never. */
    uint32_t usb_deep_timeout_ms;
    uint16_t boot_grace_s;
    bool light_sleep_enabled;
    /**
     * Sleep decisions are only reported, so a USB host does not block them
     * (light sleep still honours usb_host).
     */
    bool dry_run;
    uint8_t brightness_pct;
    /** Backlight of the dim stage when it shows home (the agent is busy). */
    uint8_t dim_pct;
    power_clock_mode_t clock_mode;
    /** Room mood idle or sleeping, or no agent traffic for 5 minutes. */
    bool agent_idle;
    /** Local night with Night dimming on. */
    bool night;
    /** Clock backlight by day on external power. */
    uint8_t clock_brightness_pct;
    /** Battery charge 0..100; read only when batt_present. */
    uint8_t batt_pct;
} power_fsm_input_t;

typedef struct {
    power_stage_t stage;
    uint8_t backlight_pct;
    bool panel_sleep;
    power_wifi_ps_t wifi_ps;
    bool hold_cpu_lock;
    bool allow_light_sleep;
    bool enter_deep_sleep;
    power_deep_reason_t deep_reason;
    bool show_lowbatt;
    /** True on the one update that starts a low-battery episode. */
    bool low_toast;
    /** POWER_FSM_LOW_PCT_1 or _2 on the update that crosses it, else 0. */
    uint8_t low_toast_pct;
    /** The resting face (clock, 40-42) replaces home. */
    bool show_clock;
    /** The going-to-sleep screen shows; an idle deep sleep follows in sleep_in_ms. */
    bool sleep_warning;
    uint32_t sleep_in_ms;
    /** True while the battery-wake decision of this boot is still open. */
    bool wake_check_pending;
    /** power_block_t bits for the idle deep sleep; 0 when it is due. */
    uint32_t blockers;
    /** Timeouts in effect (deep_ms 0 = never). */
    uint32_t dim_ms;
    uint32_t off_ms;
    uint32_t deep_ms;
    /** Time since the last user activity, and since the newer of user and agent activity. */
    uint32_t idle_ms;
    uint32_t agent_idle_ms;
} power_fsm_output_t;

typedef struct {
    bool started;
    power_stage_t stage;
    uint32_t seen_user_ms;
    bool forced;
    power_stage_t forced_stage;
    bool low_warned;
    bool crit_timing;
    uint32_t crit_since_ms;
    bool wake_done;
    bool wake_timing;
    uint32_t wake_since_ms;
    bool card_shown;
    bool card_closed;
    uint32_t card_closed_ms;
    /** Low-battery toasts already shown in this discharge (0, 1 or 2). */
    uint8_t low_pct_level;
} power_fsm_t;

void power_fsm_init(power_fsm_t *f);

/**
 * Holds stage until user activity newer than last_user_ms arrives. The
 * pending-request cap still applies.
 */
void power_fsm_force_stage(power_fsm_t *f, power_stage_t stage, uint32_t last_user_ms);

void power_fsm_update(power_fsm_t *f, const power_fsm_input_t *in, power_fsm_output_t *out);

const char *power_stage_name(power_stage_t stage);

/** Parses "active", "dim" or "off"; false for anything else. */
bool power_stage_parse(const char *name, power_stage_t *out);

/** Name of one power_block_t bit, "" for anything else. */
const char *power_block_name(uint32_t bit);

const char *power_deep_reason_name(power_deep_reason_t reason);

const char *power_clock_mode_name(power_clock_mode_t mode);
