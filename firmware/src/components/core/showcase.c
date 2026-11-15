#include "showcase.h"

#include <string.h>

static const char *const PAGE_NAMES[SHOWCASE_PAGE_COUNT] = {"menu", "moods", "reactions", "permission", "question",
                                                            "clock", "notify"};

static const showcase_act_t NONE = {.kind = SHOWCASE_ACT_NONE};

void showcase_init(showcase_t *s)
{
    memset(s, 0, sizeof(*s));
}

bool showcase_may_enter(uint8_t pending)
{
    return pending == 0;
}

bool showcase_enter(showcase_t *s, uint8_t pending, uint32_t now_ms)
{
    if (!showcase_may_enter(pending)) {
        return false;
    }
    s->active = true;
    s->page = SHOWCASE_PAGE_MENU;
    s->cursor = SHOWCASE_ROW_MOODS;
    s->mood = 0;
    s->reaction = 0;
    s->night = false;
    s->last_press_ms = now_ms;
    s->guard = false;
    return true;
}

void showcase_leave(showcase_t *s, uint32_t now_ms)
{
    if (!s->active) {
        return;
    }
    s->active = false;
    s->page = SHOWCASE_PAGE_MENU;
    s->guard = true;
    s->guard_until = now_ms + SHOWCASE_GUARD_MS;
}

void showcase_back(showcase_t *s)
{
    s->page = SHOWCASE_PAGE_MENU;
}

void showcase_open(showcase_t *s, showcase_page_t page, uint32_t now_ms)
{
    if (page < SHOWCASE_PAGE_COUNT) {
        s->page = page;
        s->night = false;
        s->last_press_ms = now_ms;
    }
}

static showcase_act_t act(showcase_act_kind_t kind, showcase_page_t page)
{
    return (showcase_act_t){.kind = kind, .page = page};
}

static showcase_act_t leave(showcase_t *s, uint32_t now_ms)
{
    showcase_leave(s, now_ms);
    return act(SHOWCASE_ACT_LEAVE, SHOWCASE_PAGE_MENU);
}

static showcase_act_t menu_open(showcase_t *s, uint32_t now_ms)
{
    static const showcase_page_t PAGES[SHOWCASE_ROW_COUNT] = {
        [SHOWCASE_ROW_MOODS] = SHOWCASE_PAGE_MOODS,       [SHOWCASE_ROW_REACTIONS] = SHOWCASE_PAGE_REACTIONS,
        [SHOWCASE_ROW_PERMISSION] = SHOWCASE_PAGE_PERMISSION, [SHOWCASE_ROW_QUESTION] = SHOWCASE_PAGE_QUESTION,
        [SHOWCASE_ROW_NOTIFY] = SHOWCASE_PAGE_NOTIFY,       [SHOWCASE_ROW_CLOCK] = SHOWCASE_PAGE_CLOCK,
        [SHOWCASE_ROW_LEAVE] = SHOWCASE_PAGE_MENU,
    };
    switch (s->cursor) {
    case SHOWCASE_ROW_NOTIFY:
        showcase_open(s, SHOWCASE_PAGE_NOTIFY, now_ms);
        return act(SHOWCASE_ACT_NOTIFY, s->page);
    case SHOWCASE_ROW_LEAVE:
        return leave(s, now_ms);
    case SHOWCASE_ROW_MOODS:
    case SHOWCASE_ROW_REACTIONS:
    case SHOWCASE_ROW_PERMISSION:
    case SHOWCASE_ROW_QUESTION:
    case SHOWCASE_ROW_CLOCK:
    case SHOWCASE_ROW_COUNT:
        break;
    }
    showcase_open(s, PAGES[s->cursor % SHOWCASE_ROW_COUNT], now_ms);
    return act(SHOWCASE_ACT_OPEN, s->page);
}

static showcase_act_t on_click(showcase_t *s, btn_id_t id, uint32_t now_ms)
{
    switch (s->page) {
    case SHOWCASE_PAGE_MENU:
        if (id == BTN_REJECT) {
            return leave(s, now_ms);
        }
        if (id == BTN_ACTION) {
            s->cursor = (showcase_row_t)((s->cursor + 1) % SHOWCASE_ROW_COUNT);
            return act(SHOWCASE_ACT_MOVED, s->page);
        }
        return menu_open(s, now_ms);
    case SHOWCASE_PAGE_MOODS:
    case SHOWCASE_PAGE_REACTIONS:
        if (id == BTN_REJECT) {
            s->page = SHOWCASE_PAGE_MENU;
            return act(SHOWCASE_ACT_BACK, s->page);
        }
        if (id == BTN_ACTION) {
            if (s->page == SHOWCASE_PAGE_MOODS) {
                s->mood = (uint8_t)((s->mood + 1) % SHOWCASE_MOODS);
            } else {
                s->reaction = (uint8_t)((s->reaction + 1) % SHOWCASE_REACTIONS);
            }
            return act(SHOWCASE_ACT_MOVED, s->page);
        }
        return act(SHOWCASE_ACT_PLAY, s->page);
    case SHOWCASE_PAGE_NOTIFY:
        if (id == BTN_REJECT) {
            s->page = SHOWCASE_PAGE_MENU;
            return act(SHOWCASE_ACT_BACK, s->page);
        }
        return id == BTN_APPROVE ? act(SHOWCASE_ACT_PLAY, s->page) : NONE;
    case SHOWCASE_PAGE_CLOCK:
        if (id == BTN_REJECT) {
            s->page = SHOWCASE_PAGE_MENU;
            return act(SHOWCASE_ACT_BACK, s->page);
        }
        if (id == BTN_ACTION) {
            s->night = !s->night;
            return act(SHOWCASE_ACT_NIGHT, s->page);
        }
        return NONE;
    case SHOWCASE_PAGE_PERMISSION:
    case SHOWCASE_PAGE_QUESTION:
    case SHOWCASE_PAGE_COUNT:
        break;
    }
    return act(SHOWCASE_ACT_CARD, s->page);
}

showcase_act_t showcase_button(showcase_t *s, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (!s->active || out->swallowed) {
        return NONE;
    }
    s->last_press_ms = now_ms;
    const bool card = s->page == SHOWCASE_PAGE_PERMISSION || s->page == SHOWCASE_PAGE_QUESTION;
    if (out->id == BTN_REJECT && out->kind == BTN_HOLD) {
        return leave(s, now_ms);
    }
    if (card) {
        return act(SHOWCASE_ACT_CARD, s->page);
    }
    if (out->kind != BTN_CLICK) {
        return NONE;
    }
    return on_click(s, out->id, now_ms);
}

showcase_act_t showcase_tick(showcase_t *s, uint32_t now_ms)
{
    if (s->guard && (int32_t)(now_ms - s->guard_until) >= 0) {
        s->guard = false;
    }
    if (s->active && (uint32_t)(now_ms - s->last_press_ms) >= SHOWCASE_IDLE_MS) {
        return leave(s, now_ms);
    }
    return NONE;
}

bool showcase_guarded(showcase_t *s, uint32_t now_ms)
{
    if (s->guard && (int32_t)(now_ms - s->guard_until) >= 0) {
        s->guard = false;
    }
    return s->active || s->guard;
}

btn_mode_t showcase_btn_mode(const showcase_t *s, btn_id_t id)
{
    if (id == BTN_REJECT) {
        return BTN_MODE_NAV_HOLD;
    }
    if ((s->page == SHOWCASE_PAGE_CLOCK && id == BTN_APPROVE) || (s->page == SHOWCASE_PAGE_NOTIFY && id == BTN_ACTION)) {
        return BTN_MODE_NONE;
    }
    return BTN_MODE_CLICK;
}

const char *showcase_page_name(showcase_page_t page)
{
    return page < SHOWCASE_PAGE_COUNT ? PAGE_NAMES[page] : "";
}

bool showcase_page_parse(const char *name, showcase_page_t *out)
{
    for (int i = 0; i < SHOWCASE_PAGE_COUNT; i++) {
        if (strcmp(name, PAGE_NAMES[i]) == 0) {
            *out = (showcase_page_t)i;
            return true;
        }
    }
    return false;
}
