#include "btn_fsm.h"

#include <string.h>

static const char *const ID_NAMES[BTN_COUNT] = {"approve", "reject", "action"};

void btn_fsm_init(btn_fsm_t *f)
{
    *f = (btn_fsm_t){0};
}

bool btn_fsm_any_down(const btn_fsm_t *f)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (f->down[i]) {
            return true;
        }
    }
    return false;
}

/* The arming ends only at a moment when nothing is held, so a press that spans it stays swallowed. */
static void update_arming(btn_fsm_t *f, uint32_t now_ms)
{
    if (f->armed && !btn_fsm_any_down(f) && (uint32_t)(now_ms - f->arm_from_ms) >= f->arm_delay_ms) {
        f->armed = false;
    }
}

void btn_fsm_arm(btn_fsm_t *f, uint32_t now_ms, uint32_t delay_ms)
{
    update_arming(f, now_ms);
    const uint32_t old_end = f->arm_from_ms + f->arm_delay_ms;
    const uint32_t new_end = now_ms + delay_ms;
    if (!f->armed || (int32_t)(new_end - old_end) > 0) {
        f->arm_from_ms = now_ms;
        f->arm_delay_ms = delay_ms;
    }
    f->armed = true;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (f->down[i]) {
            f->press_swallowed[i] = true;
        }
    }
}

bool btn_fsm_swallowing(btn_fsm_t *f, uint32_t now_ms)
{
    update_arming(f, now_ms);
    return f->armed;
}

bool btn_fsm_armed_until(const btn_fsm_t *f, uint32_t *out_ms)
{
    if (!f->armed || btn_fsm_any_down(f)) {
        return false;
    }
    *out_ms = f->arm_from_ms + f->arm_delay_ms;
    return true;
}

static uint16_t progress(uint32_t held_ms)
{
    return held_ms >= BTN_FSM_COMMIT_HOLD_MS ? 1000 : (uint16_t)(held_ms * 1000 / BTN_FSM_COMMIT_HOLD_MS);
}

static void emit(btn_fsm_out_t *out, const btn_fsm_t *f, btn_id_t id, btn_kind_t kind, uint16_t permille)
{
    *out = (btn_fsm_out_t){.id = id, .kind = kind, .permille = permille, .swallowed = f->press_swallowed[id]};
}

bool btn_fsm_down(btn_fsm_t *f, btn_id_t id, btn_mode_t mode, uint32_t now_ms, btn_fsm_out_t *out)
{
    if (id >= BTN_COUNT) {
        return false;
    }
    update_arming(f, now_ms);
    f->down[id] = true;
    f->mode[id] = mode;
    f->down_ms[id] = now_ms;
    f->fired[id] = false;
    f->press_swallowed[id] = f->armed;
    if (mode != BTN_MODE_COMMIT_HOLD || f->press_swallowed[id]) {
        return false;
    }
    emit(out, f, id, BTN_PROGRESS, 0);
    return true;
}

/* The output a press has earned by now_ms, if any; a press that is still down earns only its hold. */
static bool due_output(btn_fsm_t *f, btn_id_t id, uint32_t now_ms, bool released, btn_fsm_out_t *out)
{
    const uint32_t held = now_ms - f->down_ms[id];
    switch (f->mode[id]) {
    case BTN_MODE_NONE:
        return false;
    case BTN_MODE_CLICK:
        if (!released) {
            return false;
        }
        emit(out, f, id, BTN_CLICK, 0);
        return true;
    case BTN_MODE_NAV_HOLD:
        if (f->fired[id]) {
            return false;
        }
        if (held >= BTN_FSM_NAV_HOLD_MS) {
            f->fired[id] = true;
            emit(out, f, id, BTN_HOLD, 0);
            return true;
        }
        if (!released) {
            return false;
        }
        emit(out, f, id, BTN_CLICK, 0);
        return true;
    case BTN_MODE_COMMIT_HOLD:
        if (f->fired[id]) {
            return false;
        }
        if (held >= BTN_FSM_COMMIT_HOLD_MS) {
            f->fired[id] = true;
            emit(out, f, id, BTN_COMMIT, 1000);
            return true;
        }
        if (released) {
            emit(out, f, id, BTN_CANCELLED, progress(held));
            return true;
        }
        if (f->press_swallowed[id]) {
            return false;
        }
        emit(out, f, id, BTN_PROGRESS, progress(held));
        return true;
    }
    return false;
}

bool btn_fsm_up(btn_fsm_t *f, btn_id_t id, uint32_t now_ms, btn_fsm_out_t *out)
{
    if (id >= BTN_COUNT || !f->down[id]) {
        return false;
    }
    const bool has = due_output(f, id, now_ms, true, out);
    f->down[id] = false;
    f->fired[id] = false;
    f->press_swallowed[id] = false;
    /* Arming counts from the moment every button is up. */
    if (f->armed && !btn_fsm_any_down(f)) {
        f->arm_from_ms = now_ms;
    }
    return has;
}

size_t btn_fsm_tick(btn_fsm_t *f, uint32_t now_ms, btn_fsm_out_t out[BTN_COUNT])
{
    size_t n = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (f->down[i] && due_output(f, (btn_id_t)i, now_ms, false, &out[n])) {
            n++;
        }
    }
    return n;
}

const char *btn_id_name(btn_id_t id)
{
    return id < BTN_COUNT ? ID_NAMES[id] : "";
}

const char *btn_kind_name(btn_kind_t kind)
{
    switch (kind) {
    case BTN_CLICK:
        return "click";
    case BTN_HOLD:
        return "hold";
    case BTN_PROGRESS:
        return "progress";
    case BTN_COMMIT:
        return "commit";
    case BTN_CANCELLED:
        return "cancelled";
    }
    return "";
}

const char *btn_mode_name(btn_mode_t mode)
{
    switch (mode) {
    case BTN_MODE_NONE:
        return "none";
    case BTN_MODE_CLICK:
        return "click";
    case BTN_MODE_NAV_HOLD:
        return "nav_hold";
    case BTN_MODE_COMMIT_HOLD:
        return "commit_hold";
    }
    return "";
}

bool btn_id_parse(const char *name, btn_id_t *out)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (strcmp(name, ID_NAMES[i]) == 0) {
            *out = (btn_id_t)i;
            return true;
        }
    }
    return false;
}
