#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Time after a wake during which every button output is swallowed. */
#define BTN_FSM_WAKE_GUARD_MS  300

typedef enum {
    BTN_APPROVE,
    BTN_REJECT,
    BTN_ACTION,
    BTN_COUNT,
} btn_id_t;

/** What the button driver reports. */
typedef enum {
    BTN_RAW_DOWN,
    BTN_RAW_UP,
    BTN_RAW_LONG_START,
} btn_raw_t;

/** What the application acts on. */
typedef enum {
    BTN_CLICK,
    BTN_LONG,
} btn_kind_t;

typedef struct {
    btn_id_t id;
    btn_kind_t kind;
    bool swallowed;
} btn_fsm_out_t;

typedef struct {
    bool down[BTN_COUNT];
    bool long_fired[BTN_COUNT];
    bool press_swallowed[BTN_COUNT];
    bool armed;
    uint32_t armed_ms;
} btn_fsm_t;

void btn_fsm_init(btn_fsm_t *f);

/**
 * Starts swallowing: every output is flagged swallowed until all buttons are
 * up and BTN_FSM_WAKE_GUARD_MS passed since now_ms. Presses already down are
 * swallowed to their end.
 */
void btn_fsm_arm_swallow(btn_fsm_t *f, uint32_t now_ms);

/** True while outputs would be swallowed. */
bool btn_fsm_swallowing(btn_fsm_t *f, uint32_t now_ms);

/**
 * Feeds one driver event. Returns true and fills out when it completes a
 * click (on release, only if no long press started) or a long press.
 */
bool btn_fsm_raw(btn_fsm_t *f, btn_id_t id, btn_raw_t raw, uint32_t now_ms, btn_fsm_out_t *out);

/** Applies the swallow rule to an output that did not come from a physical press (console injection). */
void btn_fsm_inject(btn_fsm_t *f, btn_id_t id, btn_kind_t kind, uint32_t now_ms, btn_fsm_out_t *out);

const char *btn_id_name(btn_id_t id);
const char *btn_kind_name(btn_kind_t kind);

/** Parses "approve", "reject" or "action"; false for anything else. */
bool btn_id_parse(const char *name, btn_id_t *out);
