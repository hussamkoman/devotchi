#include "btn_fsm.h"

#include <string.h>

static const char *const ID_NAMES[BTN_COUNT] = {"approve", "reject", "action"};

void btn_fsm_init(btn_fsm_t *f)
{
    *f = (btn_fsm_t){0};
}

static bool any_down(const btn_fsm_t *f)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (f->down[i]) {
            return true;
        }
    }
    return false;
}

/* The guard ends only at a moment when nothing is held, so a press that spans it stays swallowed. */
static void update_guard(btn_fsm_t *f, uint32_t now_ms)
{
    if (f->armed && !any_down(f) && (uint32_t)(now_ms - f->armed_ms) >= BTN_FSM_WAKE_GUARD_MS) {
        f->armed = false;
    }
}

void btn_fsm_arm_swallow(btn_fsm_t *f, uint32_t now_ms)
{
    f->armed = true;
    f->armed_ms = now_ms;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (f->down[i]) {
            f->press_swallowed[i] = true;
        }
    }
}

bool btn_fsm_swallowing(btn_fsm_t *f, uint32_t now_ms)
{
    update_guard(f, now_ms);
    return f->armed;
}

bool btn_fsm_raw(btn_fsm_t *f, btn_id_t id, btn_raw_t raw, uint32_t now_ms, btn_fsm_out_t *out)
{
    if (id >= BTN_COUNT) {
        return false;
    }
    switch (raw) {
    case BTN_RAW_DOWN:
        update_guard(f, now_ms);
        f->down[id] = true;
        f->long_fired[id] = false;
        f->press_swallowed[id] = f->armed;
        return false;
    case BTN_RAW_LONG_START:
        if (!f->down[id] || f->long_fired[id]) {
            return false;
        }
        f->long_fired[id] = true;
        *out = (btn_fsm_out_t){.id = id, .kind = BTN_LONG, .swallowed = f->press_swallowed[id]};
        return true;
    case BTN_RAW_UP: {
        if (!f->down[id]) {
            return false;
        }
        const bool click = !f->long_fired[id];
        const bool swallowed = f->press_swallowed[id];
        f->down[id] = false;
        f->long_fired[id] = false;
        f->press_swallowed[id] = false;
        if (!click) {
            return false;
        }
        *out = (btn_fsm_out_t){.id = id, .kind = BTN_CLICK, .swallowed = swallowed};
        return true;
    }
    }
    return false;
}

void btn_fsm_inject(btn_fsm_t *f, btn_id_t id, btn_kind_t kind, uint32_t now_ms, btn_fsm_out_t *out)
{
    update_guard(f, now_ms);
    *out = (btn_fsm_out_t){.id = id, .kind = kind, .swallowed = f->armed};
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
    case BTN_LONG:
        return "long";
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
