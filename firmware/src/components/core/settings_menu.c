#include "settings_menu.h"

#include <stdio.h>
#include <string.h>

static const uint16_t BRIGHTNESS[] = {25, 50, 75, 100};
/* 0 is "Never" and comes last, as on screen. */
static const uint16_t SCREEN_TIMEOUT_S[] = {15, 30, 60, 120, 0};
static const uint16_t DEEP_SLEEP_MIN[] = {10, 20, 60, 0};
static const uint16_t SHOW_CLOCK[] = {SETTINGS_CLOCK_RESTING, SETTINGS_CLOCK_ALWAYS_USB, SETTINGS_CLOCK_NEVER};
static const uint16_t CLOCK_BRIGHTNESS[] = {10, 20, 30, 50};
static const uint16_t H24[] = {1, 0};
static const uint16_t ON_OFF[] = {1, 0};
static const uint16_t HAPTICS[] = {0, 1, 2};

typedef struct {
    const char *key;
    const char *label;
    settings_kind_t kind;
    const uint16_t *choices;
    size_t count;
    settings_list_t list;
} item_def_t;

#define CHOICES(a) (a), (sizeof(a) / sizeof((a)[0]))

static const item_def_t ITEMS[SETTINGS_ITEM_COUNT] = {
    [SETTINGS_ITEM_PET] = {"pet", "Pet", SETTINGS_KIND_ACTION, NULL, 0, 0},
    [SETTINGS_ITEM_DISPLAY] = {"display", "Display", SETTINGS_KIND_LIST, NULL, 0, SETTINGS_LIST_DISPLAY},
    [SETTINGS_ITEM_CLOCK] = {"clock", "Clock", SETTINGS_KIND_LIST, NULL, 0, SETTINGS_LIST_CLOCK},
    [SETTINGS_ITEM_HAPTICS_LED] = {"haptics_led", "Haptics and LED", SETTINGS_KIND_LIST, NULL, 0,
                                   SETTINGS_LIST_HAPTICS},
    [SETTINGS_ITEM_DESK] = {"desk", "Desk mode", SETTINGS_KIND_TOGGLE, NULL, 0, 0},
    [SETTINGS_ITEM_NETWORK] = {"network", "Network", SETTINGS_KIND_LIST, NULL, 0, SETTINGS_LIST_NETWORK},
    [SETTINGS_ITEM_SHOWCASE] = {"showcase", "Showcase", SETTINGS_KIND_ACTION, NULL, 0, 0},
    [SETTINGS_ITEM_SYSTEM] = {"system", "System info", SETTINGS_KIND_ACTION, NULL, 0, 0},
    [SETTINGS_ITEM_ABOUT] = {"about", "About", SETTINGS_KIND_ACTION, NULL, 0, 0},
    [SETTINGS_ITEM_BRIGHTNESS] = {"brightness", "Brightness", SETTINGS_KIND_VALUE, CHOICES(BRIGHTNESS), 0},
    [SETTINGS_ITEM_SCREEN_TIMEOUT] = {"screen_timeout", "Screen timeout", SETTINGS_KIND_VALUE,
                                      CHOICES(SCREEN_TIMEOUT_S), 0},
    [SETTINGS_ITEM_DEEP_SLEEP] = {"deep_sleep", "Deep sleep", SETTINGS_KIND_VALUE, CHOICES(DEEP_SLEEP_MIN), 0},
    [SETTINGS_ITEM_SHOW_CLOCK] = {"show_clock", "Show clock", SETTINGS_KIND_VALUE, CHOICES(SHOW_CLOCK), 0},
    [SETTINGS_ITEM_CLOCK_BRIGHTNESS] = {"clock_brightness", "Clock brightness", SETTINGS_KIND_VALUE,
                                        CHOICES(CLOCK_BRIGHTNESS), 0},
    [SETTINGS_ITEM_H24] = {"h24", "12/24 h", SETTINGS_KIND_VALUE, CHOICES(H24), 0},
    [SETTINGS_ITEM_NIGHT] = {"night", "Night dimming", SETTINGS_KIND_VALUE, CHOICES(ON_OFF), 0},
    [SETTINGS_ITEM_HAPTICS] = {"haptics", "Haptics", SETTINGS_KIND_VALUE, CHOICES(HAPTICS), 0},
    [SETTINGS_ITEM_LED] = {"led", "LED", SETTINGS_KIND_VALUE, CHOICES(ON_OFF), 0},
    [SETTINGS_ITEM_CLICKS] = {"clicks", "Button clicks", SETTINGS_KIND_VALUE, CHOICES(ON_OFF), 0},
    [SETTINGS_ITEM_WIFI_SETUP] = {"wifi_setup", "Wi-Fi setup", SETTINGS_KIND_ACTION, NULL, 0, 0},
    [SETTINGS_ITEM_UNPAIR] = {"unpair", "Unpair", SETTINGS_KIND_DESTRUCTIVE, NULL, 0, 0},
    [SETTINGS_ITEM_FACTORY_RESET] = {"factory_reset", "Factory reset", SETTINGS_KIND_DESTRUCTIVE, NULL, 0, 0},
    [SETTINGS_ITEM_SETTINGS] = {"settings", "Settings", SETTINGS_KIND_LIST, NULL, 0, SETTINGS_LIST_TOP},
};

static const settings_item_t QUICK[] = {SETTINGS_ITEM_SETTINGS, SETTINGS_ITEM_SHOWCASE, SETTINGS_ITEM_DESK};
static const settings_item_t TOP[] = {
    SETTINGS_ITEM_PET,  SETTINGS_ITEM_DISPLAY, SETTINGS_ITEM_CLOCK,    SETTINGS_ITEM_HAPTICS_LED,
    SETTINGS_ITEM_DESK, SETTINGS_ITEM_NETWORK, SETTINGS_ITEM_SHOWCASE, SETTINGS_ITEM_SYSTEM,
    SETTINGS_ITEM_ABOUT,
};
static const settings_item_t DISPLAY[] = {SETTINGS_ITEM_BRIGHTNESS, SETTINGS_ITEM_SCREEN_TIMEOUT,
                                          SETTINGS_ITEM_DEEP_SLEEP};
static const settings_item_t CLOCK[] = {SETTINGS_ITEM_SHOW_CLOCK, SETTINGS_ITEM_CLOCK_BRIGHTNESS, SETTINGS_ITEM_H24,
                                        SETTINGS_ITEM_NIGHT};
static const settings_item_t HAPTICS_LED[] = {SETTINGS_ITEM_HAPTICS, SETTINGS_ITEM_CLICKS, SETTINGS_ITEM_LED};
static const settings_item_t NETWORK[] = {SETTINGS_ITEM_WIFI_SETUP, SETTINGS_ITEM_UNPAIR,
                                          SETTINGS_ITEM_FACTORY_RESET};

typedef struct {
    const char *key;
    const char *title;
    const settings_item_t *items;
    size_t count;
    /** Where REJECT goes; SETTINGS_LIST_COUNT exits the menu. */
    settings_list_t parent;
} list_def_t;

static const list_def_t LISTS[SETTINGS_LIST_COUNT] = {
    [SETTINGS_LIST_QUICK] = {"quick", "Menu", CHOICES(QUICK), SETTINGS_LIST_COUNT},
    [SETTINGS_LIST_TOP] = {"top", "Settings", CHOICES(TOP), SETTINGS_LIST_COUNT},
    [SETTINGS_LIST_DISPLAY] = {"display", "Display", CHOICES(DISPLAY), SETTINGS_LIST_TOP},
    [SETTINGS_LIST_CLOCK] = {"clock", "Clock", CHOICES(CLOCK), SETTINGS_LIST_TOP},
    [SETTINGS_LIST_HAPTICS] = {"haptics_led", "Haptics and LED", CHOICES(HAPTICS_LED), SETTINGS_LIST_TOP},
    [SETTINGS_LIST_NETWORK] = {"network", "Network", CHOICES(NETWORK), SETTINGS_LIST_TOP},
};

static const settings_act_t NONE = {.kind = SETTINGS_ACT_NONE};

static bool is_value(settings_item_t item)
{
    return item >= SETTINGS_VALUE_FIRST && item < SETTINGS_VALUE_FIRST + SETTINGS_VALUE_COUNT;
}

const settings_item_t *settings_list_items(settings_list_t list, size_t *count)
{
    if (list >= SETTINGS_LIST_COUNT) {
        *count = 0;
        return NULL;
    }
    *count = LISTS[list].count;
    return LISTS[list].items;
}

const char *settings_list_title(settings_list_t list)
{
    return list < SETTINGS_LIST_COUNT ? LISTS[list].title : "";
}

const char *settings_list_key(settings_list_t list)
{
    return list < SETTINGS_LIST_COUNT ? LISTS[list].key : "";
}

settings_kind_t settings_item_kind(settings_item_t item)
{
    return item < SETTINGS_ITEM_COUNT ? ITEMS[item].kind : SETTINGS_KIND_ACTION;
}

const char *settings_item_label(settings_item_t item)
{
    return item < SETTINGS_ITEM_COUNT ? ITEMS[item].label : "";
}

const char *settings_item_key(settings_item_t item)
{
    return item < SETTINGS_ITEM_COUNT ? ITEMS[item].key : "";
}

settings_list_t settings_item_list(settings_item_t item)
{
    return item < SETTINGS_ITEM_COUNT && ITEMS[item].kind == SETTINGS_KIND_LIST ? ITEMS[item].list
                                                                                 : SETTINGS_LIST_COUNT;
}

const uint16_t *settings_item_choices(settings_item_t item, size_t *count)
{
    if (!is_value(item)) {
        *count = 0;
        return NULL;
    }
    *count = ITEMS[item].count;
    return ITEMS[item].choices;
}

static int choice_index(settings_item_t item, uint16_t value)
{
    size_t count;
    const uint16_t *choices = settings_item_choices(item, &count);
    for (size_t i = 0; i < count; i++) {
        if (choices[i] == value) {
            return (int)i;
        }
    }
    return -1;
}

bool settings_item_allowed(settings_item_t item, uint16_t value)
{
    return choice_index(item, value) >= 0;
}

void settings_value_parts(settings_item_t item, uint16_t value, char num[SETTINGS_VALUE_TEXT_SIZE],
                          char unit[SETTINGS_VALUE_TEXT_SIZE])
{
    static const char *const HAPTIC_NAMES[] = {"Off", "Gentle", "Full"};
    static const char *const CLOCK_NAMES[] = {"When resting", "Always on USB", "Never"};
    num[0] = '\0';
    unit[0] = '\0';
    switch (item) {
    case SETTINGS_ITEM_BRIGHTNESS:
    case SETTINGS_ITEM_CLOCK_BRIGHTNESS:
        snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "%u", value);
        snprintf(unit, SETTINGS_VALUE_TEXT_SIZE, "%%");
        break;
    case SETTINGS_ITEM_SCREEN_TIMEOUT:
    case SETTINGS_ITEM_DEEP_SLEEP:
        if (value == 0) {
            snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "Never");
        } else {
            snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "%u", value);
            snprintf(unit, SETTINGS_VALUE_TEXT_SIZE, item == SETTINGS_ITEM_DEEP_SLEEP ? "min" : "s");
        }
        break;
    case SETTINGS_ITEM_SHOW_CLOCK:
        snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "%s", value < 3 ? CLOCK_NAMES[value] : "?");
        break;
    case SETTINGS_ITEM_H24:
        snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "%s", value != 0 ? "24" : "12");
        snprintf(unit, SETTINGS_VALUE_TEXT_SIZE, "h");
        break;
    case SETTINGS_ITEM_HAPTICS:
        snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "%s", value < 3 ? HAPTIC_NAMES[value] : "?");
        break;
    case SETTINGS_ITEM_NIGHT:
    case SETTINGS_ITEM_LED:
    case SETTINGS_ITEM_CLICKS:
    case SETTINGS_ITEM_DESK:
        snprintf(num, SETTINGS_VALUE_TEXT_SIZE, "%s", value != 0 ? "On" : "Off");
        break;
    default:
        break;
    }
}

void settings_value_text(settings_item_t item, uint16_t value, char out[SETTINGS_VALUE_TEXT_SIZE])
{
    char num[SETTINGS_VALUE_TEXT_SIZE];
    char unit[SETTINGS_VALUE_TEXT_SIZE];
    settings_value_parts(item, value, num, unit);
    snprintf(out, SETTINGS_VALUE_TEXT_SIZE, unit[0] != '\0' ? "%s %s" : "%s%s", num, unit);
}

const char *settings_view_name(settings_view_t view)
{
    switch (view) {
    case SETTINGS_VIEW_EDITOR:
        return "editor";
    case SETTINGS_VIEW_CONFIRM:
        return "confirm";
    case SETTINGS_VIEW_LIST:
        break;
    }
    return "list";
}

void settings_menu_open(settings_menu_t *m, settings_list_t list, const uint16_t values[SETTINGS_VALUE_COUNT],
                        bool desk, uint32_t now_ms)
{
    *m = (settings_menu_t){
        .desk = desk,
        .list = list < SETTINGS_LIST_COUNT ? list : SETTINGS_LIST_TOP,
        .last_ms = now_ms,
    };
    memcpy(m->values, values, sizeof(m->values));
}

static bool cursor_on(settings_menu_t *m, settings_list_t list, const char *key, size_t key_len)
{
    size_t count;
    const settings_item_t *items = settings_list_items(list, &count);
    for (size_t i = 0; i < count; i++) {
        const char *k = settings_item_key(items[i]);
        if (strlen(k) == key_len && strncmp(k, key, key_len) == 0) {
            m->cursor = (uint8_t)i;
            return true;
        }
    }
    return false;
}

bool settings_menu_open_path(settings_menu_t *m, const char *path, const uint16_t values[SETTINGS_VALUE_COUNT],
                             bool desk, uint32_t now_ms)
{
    const char *slash = strchr(path, '/');
    const size_t list_len = slash != NULL ? (size_t)(slash - path) : strlen(path);
    for (settings_list_t l = 0; l < SETTINGS_LIST_COUNT; l++) {
        const char *k = LISTS[l].key;
        if (strlen(k) != list_len || strncmp(k, path, list_len) != 0) {
            continue;
        }
        settings_menu_t next;
        settings_menu_open(&next, l, values, desk, now_ms);
        if (slash != NULL) {
            if (!cursor_on(&next, l, slash + 1, strlen(slash + 1))) {
                return false;
            }
            const settings_item_t item = settings_menu_item(&next);
            if (is_value(item)) {
                next.view = SETTINGS_VIEW_EDITOR;
                next.edit_old = next.values[item - SETTINGS_VALUE_FIRST];
            } else if (settings_item_kind(item) == SETTINGS_KIND_DESTRUCTIVE) {
                next.view = SETTINGS_VIEW_CONFIRM;
            }
        }
        *m = next;
        return true;
    }
    return false;
}

void settings_menu_sync(settings_menu_t *m, bool desk, bool showcase_blocked)
{
    m->desk = desk;
    m->showcase_blocked = showcase_blocked;
}

settings_item_t settings_menu_item(const settings_menu_t *m)
{
    size_t count;
    const settings_item_t *items = settings_list_items(m->list, &count);
    return m->cursor < count ? items[m->cursor] : SETTINGS_ITEM_COUNT;
}

uint16_t settings_menu_value(const settings_menu_t *m, settings_item_t item)
{
    if (item == SETTINGS_ITEM_DESK) {
        return m->desk ? 1 : 0;
    }
    return is_value(item) ? m->values[item - SETTINGS_VALUE_FIRST] : 0;
}

static settings_act_t act(settings_act_kind_t kind, settings_item_t item, uint16_t value)
{
    return (settings_act_t){.kind = kind, .item = item, .value = value};
}

/* Opens list with the cursor on item (the row a sub-list was opened from), or on the first row. */
static void enter_list(settings_menu_t *m, settings_list_t list, settings_item_t item)
{
    m->list = list;
    m->cursor = 0;
    m->view = SETTINGS_VIEW_LIST;
    size_t count;
    const settings_item_t *items = settings_list_items(list, &count);
    for (size_t i = 0; i < count; i++) {
        if (items[i] == item) {
            m->cursor = (uint8_t)i;
        }
    }
}

/* The sub-list's row in its parent, so Back lands on it. */
static settings_item_t item_opening(settings_list_t list)
{
    for (settings_item_t i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (ITEMS[i].kind == SETTINGS_KIND_LIST && ITEMS[i].list == list) {
            return i;
        }
    }
    return SETTINGS_ITEM_COUNT;
}

static settings_act_t open_row(settings_menu_t *m)
{
    const settings_item_t item = settings_menu_item(m);
    switch (settings_item_kind(item)) {
    case SETTINGS_KIND_LIST:
        enter_list(m, settings_item_list(item), SETTINGS_ITEM_COUNT);
        return act(SETTINGS_ACT_MOVED, item, 0);
    case SETTINGS_KIND_VALUE:
        m->view = SETTINGS_VIEW_EDITOR;
        m->edit_old = m->values[item - SETTINGS_VALUE_FIRST];
        return act(SETTINGS_ACT_MOVED, item, m->edit_old);
    case SETTINGS_KIND_TOGGLE:
        return act(SETTINGS_ACT_DESK, item, m->desk ? 0 : 1);
    case SETTINGS_KIND_DESTRUCTIVE:
        m->view = SETTINGS_VIEW_CONFIRM;
        return act(SETTINGS_ACT_MOVED, item, 0);
    case SETTINGS_KIND_ACTION:
        if (item == SETTINGS_ITEM_SHOWCASE && m->showcase_blocked) {
            return act(SETTINGS_ACT_REFUSED, item, 0);
        }
        return act(SETTINGS_ACT_EXECUTE, item, 0);
    }
    return NONE;
}

static settings_act_t list_button(settings_menu_t *m, const btn_fsm_out_t *out)
{
    if (out->kind != BTN_CLICK) {
        return NONE;
    }
    size_t count;
    settings_list_items(m->list, &count);
    switch (out->id) {
    case BTN_REJECT: {
        const settings_list_t parent = LISTS[m->list].parent;
        if (parent == SETTINGS_LIST_COUNT) {
            return act(SETTINGS_ACT_EXIT, settings_menu_item(m), 0);
        }
        const settings_item_t from = item_opening(m->list);
        enter_list(m, parent, from);
        return act(SETTINGS_ACT_MOVED, from, 0);
    }
    case BTN_ACTION:
        m->cursor = (uint8_t)((m->cursor + 1u) % count);
        return act(SETTINGS_ACT_MOVED, settings_menu_item(m), 0);
    case BTN_APPROVE:
        return open_row(m);
    case BTN_COUNT:
        break;
    }
    return NONE;
}

static settings_act_t cancel_edit(settings_menu_t *m)
{
    const settings_item_t item = settings_menu_item(m);
    m->values[item - SETTINGS_VALUE_FIRST] = m->edit_old;
    m->view = SETTINGS_VIEW_LIST;
    return act(SETTINGS_ACT_CANCEL, item, m->edit_old);
}

static settings_act_t editor_button(settings_menu_t *m, const btn_fsm_out_t *out)
{
    if (out->kind != BTN_CLICK) {
        return NONE;
    }
    const settings_item_t item = settings_menu_item(m);
    uint16_t *value = &m->values[item - SETTINGS_VALUE_FIRST];
    switch (out->id) {
    case BTN_REJECT:
        return cancel_edit(m);
    case BTN_ACTION: {
        size_t count;
        const uint16_t *choices = settings_item_choices(item, &count);
        /* A stored value outside the table starts the cycle at the first choice. */
        const int i = choice_index(item, *value);
        *value = choices[(size_t)(i + 1) % count];
        return act(SETTINGS_ACT_PREVIEW, item, *value);
    }
    case BTN_APPROVE:
        m->view = SETTINGS_VIEW_LIST;
        return act(SETTINGS_ACT_SAVE, item, *value);
    case BTN_COUNT:
        break;
    }
    return NONE;
}

static settings_act_t confirm_button(settings_menu_t *m, const btn_fsm_out_t *out)
{
    const settings_item_t item = settings_menu_item(m);
    if (out->id == BTN_APPROVE) {
        switch (out->kind) {
        case BTN_PROGRESS:
            return act(SETTINGS_ACT_PROGRESS, item, out->permille > 0 ? out->permille : 1);
        case BTN_CANCELLED:
        case BTN_CLICK:
            return act(SETTINGS_ACT_NUDGE, item, 0);
        case BTN_COMMIT:
            m->view = SETTINGS_VIEW_LIST;
            return act(SETTINGS_ACT_EXECUTE, item, 0);
        case BTN_HOLD:
            break;
        }
        return NONE;
    }
    if (out->id == BTN_REJECT && out->kind == BTN_CLICK) {
        m->view = SETTINGS_VIEW_LIST;
        return act(SETTINGS_ACT_MOVED, item, 0);
    }
    return NONE;
}

settings_act_t settings_menu_button(settings_menu_t *m, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (out->swallowed) {
        return NONE;
    }
    m->last_ms = now_ms;
    switch (m->view) {
    case SETTINGS_VIEW_EDITOR:
        return editor_button(m, out);
    case SETTINGS_VIEW_CONFIRM:
        return confirm_button(m, out);
    case SETTINGS_VIEW_LIST:
        break;
    }
    return list_button(m, out);
}

static uint32_t idle_ms(settings_view_t view)
{
    switch (view) {
    case SETTINGS_VIEW_EDITOR:
        return SETTINGS_EDITOR_IDLE_MS;
    case SETTINGS_VIEW_CONFIRM:
        return SETTINGS_CONFIRM_IDLE_MS;
    case SETTINGS_VIEW_LIST:
        break;
    }
    return SETTINGS_LIST_IDLE_MS;
}

uint32_t settings_menu_deadline(const settings_menu_t *m)
{
    const uint32_t at = m->last_ms + idle_ms(m->view);
    return at != 0 ? at : 1;
}

settings_act_t settings_menu_tick(settings_menu_t *m, uint32_t now_ms)
{
    if ((int32_t)(now_ms - settings_menu_deadline(m)) < 0) {
        return NONE;
    }
    const settings_item_t item = settings_menu_item(m);
    m->last_ms = now_ms;
    switch (m->view) {
    case SETTINGS_VIEW_EDITOR:
        return cancel_edit(m);
    case SETTINGS_VIEW_CONFIRM:
        m->view = SETTINGS_VIEW_LIST;
        return act(SETTINGS_ACT_MOVED, item, 0);
    case SETTINGS_VIEW_LIST:
        break;
    }
    return act(SETTINGS_ACT_EXIT, item, 0);
}

btn_mode_t settings_menu_btn_mode(const settings_menu_t *m, btn_id_t id)
{
    if (m->view == SETTINGS_VIEW_CONFIRM) {
        return id == BTN_APPROVE ? BTN_MODE_COMMIT_HOLD : id == BTN_REJECT ? BTN_MODE_CLICK : BTN_MODE_NONE;
    }
    return BTN_MODE_CLICK;
}
