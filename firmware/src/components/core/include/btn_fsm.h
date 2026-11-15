#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Thresholds of design-system 6. */
/** A navigation hold fires when the button has been down this long; an earlier release is a click. */
#define BTN_FSM_NAV_HOLD_MS     600
/** A commit hold fires at the end of this time; an earlier release cancels it. */
#define BTN_FSM_COMMIT_HOLD_MS  1200
/** The app ticks the machine at this period while a button is down. */
#define BTN_FSM_TICK_MS         30
/** Arming delay after a wake: the waking press only wakes. */
#define BTN_FSM_WAKE_GUARD_MS   300

typedef enum {
    BTN_APPROVE,
    BTN_REJECT,
    BTN_ACTION,
    BTN_COUNT,
} btn_id_t;

/** What a press on one button may produce; the app chooses it per button before the press, and it is latched at DOWN. */
typedef enum {
    /** The button has no tab: the press produces nothing. */
    BTN_MODE_NONE,
    /** CLICK on release, however long the press. */
    BTN_MODE_CLICK,
    /** CLICK on a release before BTN_FSM_NAV_HOLD_MS, else HOLD at the threshold. */
    BTN_MODE_NAV_HOLD,
    /** No click: PROGRESS while down, COMMIT at BTN_FSM_COMMIT_HOLD_MS, CANCELLED on an earlier release. */
    BTN_MODE_COMMIT_HOLD,
} btn_mode_t;

/** What the button driver reports. */
typedef enum {
    BTN_RAW_DOWN,
    BTN_RAW_UP,
} btn_raw_t;

/** What the application acts on. */
typedef enum {
    BTN_CLICK,
    BTN_HOLD,
    /** Commit hold under way; permille 0..999 of BTN_FSM_COMMIT_HOLD_MS. */
    BTN_PROGRESS,
    BTN_COMMIT,
    /** A commit hold released early; permille is how far it got. */
    BTN_CANCELLED,
} btn_kind_t;

typedef struct {
    btn_id_t id;
    btn_kind_t kind;
    uint16_t permille;
    /** The press began before the machine was armed (or during the wake guard): only log it. */
    bool swallowed;
} btn_fsm_out_t;

typedef struct {
    bool down[BTN_COUNT];
    btn_mode_t mode[BTN_COUNT];
    uint32_t down_ms[BTN_COUNT];
    /** HOLD or COMMIT already reported for this press. */
    bool fired[BTN_COUNT];
    bool press_swallowed[BTN_COUNT];
    bool armed;
    /** The delay runs from here; it restarts when the last button is released. */
    uint32_t arm_from_ms;
    uint32_t arm_delay_ms;
} btn_fsm_t;

void btn_fsm_init(btn_fsm_t *f);

/**
 * Arms the machine: every output is flagged swallowed until all buttons are
 * up and delay_ms passed after that. Presses already down are swallowed to
 * their end. An arming that would end later is kept.
 */
void btn_fsm_arm(btn_fsm_t *f, uint32_t now_ms, uint32_t delay_ms);

/** True while outputs would be swallowed. */
bool btn_fsm_swallowing(btn_fsm_t *f, uint32_t now_ms);

/** When the arming ends if no button goes down before; false when not armed or while a button is down. */
bool btn_fsm_armed_until(const btn_fsm_t *f, uint32_t *out_ms);

bool btn_fsm_any_down(const btn_fsm_t *f);

/** A press begins in mode; true and out filled for the first PROGRESS of a commit hold. */
bool btn_fsm_down(btn_fsm_t *f, btn_id_t id, btn_mode_t mode, uint32_t now_ms, btn_fsm_out_t *out);

/** A press ends; true and out filled for its CLICK, HOLD, COMMIT or CANCELLED. */
bool btn_fsm_up(btn_fsm_t *f, btn_id_t id, uint32_t now_ms, btn_fsm_out_t *out);

/** Time passes: fires holds and commits that are due and reports progress. Returns the number of outputs. */
size_t btn_fsm_tick(btn_fsm_t *f, uint32_t now_ms, btn_fsm_out_t out[BTN_COUNT]);

const char *btn_id_name(btn_id_t id);
const char *btn_kind_name(btn_kind_t kind);
const char *btn_mode_name(btn_mode_t mode);

/** Parses "approve", "reject" or "action"; false for anything else. */
bool btn_id_parse(const char *name, btn_id_t *out);
