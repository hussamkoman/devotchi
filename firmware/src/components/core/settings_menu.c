#include "settings_menu.h"

#include <stdio.h>
#include <string.h>

static const uint16_t BRIGHTNESS[] = {25, 50, 75, 100};
static const uint16_t SCREEN_TIMEOUT_S[] = {15, 30, 60, 120};
/* 0 is "never" and comes last, as on screen. */
static const uint16_t DEEP_SLEEP_MIN[] = {10, 20, 60, 0};
static const uint16_t HAPTICS[] = {0, 1, 2};
static const uint16_t ON_OFF[] = {0, 1};
static const uint16_t MODES[] = {SETTINGS_MODE_ASK, SETTINGS_MODE_DESK};

typedef struct {
    const char *key;
    const char *label;
    const uint16_t *choices;
    size_t count;
} row_def_t;

#define CHOICES(a) (a), (sizeof(a) / sizeof((a)[0]))

static const row_def_t ROWS[SETTINGS_ROW_COUNT] = {
    [SETTINGS_ROW_BRIGHTNESS] = {"brightness", "Brightness", CHOICES(BRIGHTNESS)},
    [SETTINGS_ROW_SCREEN_TIMEOUT] = {"screen_timeout", "Screen timeout", CHOICES(SCREEN_TIMEOUT_S)},
    [SETTINGS_ROW_DEEP_SLEEP] = {"deep_sleep", "Deep sleep", CHOICES(DEEP_SLEEP_MIN)},
    [SETTINGS_ROW_HAPTICS] = {"haptics", "Haptics", CHOICES(HAPTICS)},
    [SETTINGS_ROW_LED] = {"led", "LED", CHOICES(ON_OFF)},
    [SETTINGS_ROW_MODE] = {"mode", "Mode", CHOICES(MODES)},
    [SETTINGS_ROW_PET] = {"pet", "Pet", NULL, 0},
    [SETTINGS_ROW_WIFI_SETUP] = {"wifi_setup", "WiFi setup", NULL, 0},
    [SETTINGS_ROW_UNPAIR] = {"unpair", "Unpair", NULL, 0},
    [SETTINGS_ROW_FACTORY_RESET] = {"factory_reset", "Factory reset", NULL, 0},
    [SETTINGS_ROW_ABOUT] = {"about", "About", NULL, 0},
};

static const settings_act_t NONE = {.kind = SETTINGS_ACT_NONE};

void settings_menu_open(settings_menu_t *m, const uint16_t values[SETTINGS_VALUE_ROWS])
{
    *m = (settings_menu_t){.cursor = SETTINGS_ROW_BRIGHTNESS};
    memcpy(m->values, values, sizeof(m->values));
}

void settings_menu_sync(settings_menu_t *m, settings_row_t row, uint16_t value)
{
    if (row < SETTINGS_VALUE_ROWS) {
        m->values[row] = value;
    }
}

bool settings_menu_needs_confirm(settings_row_t row)
{
    return row == SETTINGS_ROW_WIFI_SETUP || row == SETTINGS_ROW_UNPAIR || row == SETTINGS_ROW_FACTORY_RESET;
}

const uint16_t *settings_menu_choices(settings_row_t row, size_t *count)
{
    if (row >= SETTINGS_ROW_COUNT || ROWS[row].choices == NULL) {
        *count = 0;
        return NULL;
    }
    *count = ROWS[row].count;
    return ROWS[row].choices;
}

static int choice_index(settings_row_t row, uint16_t value)
{
    size_t count;
    const uint16_t *choices = settings_menu_choices(row, &count);
    for (size_t i = 0; i < count; i++) {
        if (choices[i] == value) {
            return (int)i;
        }
    }
    return -1;
}

bool settings_menu_allowed(settings_row_t row, uint16_t value)
{
    return choice_index(row, value) >= 0;
}

const char *settings_menu_label(settings_row_t row)
{
    return row < SETTINGS_ROW_COUNT ? ROWS[row].label : "";
}

const char *settings_menu_key(settings_row_t row)
{
    return row < SETTINGS_ROW_COUNT ? ROWS[row].key : "";
}

void settings_menu_value_text(settings_row_t row, uint16_t value, char out[SETTINGS_VALUE_TEXT_SIZE])
{
    static const char *const HAPTIC_NAMES[] = {"Off", "Gentle", "Full"};
    out[0] = '\0';
    switch (row) {
    case SETTINGS_ROW_BRIGHTNESS:
        snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "%u %%", value);
        break;
    case SETTINGS_ROW_SCREEN_TIMEOUT:
        snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "%u s", value);
        break;
    case SETTINGS_ROW_DEEP_SLEEP:
        if (value == 0) {
            snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "Never");
        } else {
            snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "%u min", value);
        }
        break;
    case SETTINGS_ROW_HAPTICS:
        snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "%s", value < 3 ? HAPTIC_NAMES[value] : "?");
        break;
    case SETTINGS_ROW_LED:
        snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "%s", value != 0 ? "On" : "Off");
        break;
    case SETTINGS_ROW_MODE:
        snprintf(out, SETTINGS_VALUE_TEXT_SIZE, "%s", value == SETTINGS_MODE_DESK ? "Desk" : "Ask");
        break;
    case SETTINGS_ROW_PET:
    case SETTINGS_ROW_WIFI_SETUP:
    case SETTINGS_ROW_UNPAIR:
    case SETTINGS_ROW_FACTORY_RESET:
    case SETTINGS_ROW_ABOUT:
    case SETTINGS_ROW_COUNT:
        break;
    }
}

static settings_act_t move(settings_menu_t *m, int step)
{
    const bool was_confirming = m->confirming;
    m->confirming = false;
    m->cursor = (settings_row_t)(((int)m->cursor + step + SETTINGS_ROW_COUNT) % SETTINGS_ROW_COUNT);
    return (settings_act_t){
        .kind = was_confirming ? SETTINGS_ACT_CONFIRM_CANCELLED : SETTINGS_ACT_MOVED,
        .row = m->cursor,
    };
}

static settings_act_t activate(settings_menu_t *m, uint32_t now_ms)
{
    const settings_row_t row = m->cursor;
    if (row < SETTINGS_VALUE_ROWS) {
        size_t count;
        const uint16_t *choices = settings_menu_choices(row, &count);
        /* A stored value outside the table starts the cycle at the first choice. */
        const int i = choice_index(row, m->values[row]);
        m->values[row] = choices[(size_t)(i + 1) % count];
        return (settings_act_t){.kind = SETTINGS_ACT_VALUE, .row = row, .value = m->values[row]};
    }
    if (!settings_menu_needs_confirm(row)) {
        return (settings_act_t){.kind = SETTINGS_ACT_EXECUTE, .row = row};
    }
    if (m->confirming && (int32_t)(m->confirm_until - now_ms) > 0) {
        m->confirming = false;
        return (settings_act_t){.kind = SETTINGS_ACT_EXECUTE, .row = row};
    }
    m->confirming = true;
    m->confirm_until = now_ms + SETTINGS_CONFIRM_MS;
    return (settings_act_t){.kind = SETTINGS_ACT_CONFIRM_ASK, .row = row};
}

settings_act_t settings_menu_button(settings_menu_t *m, const btn_fsm_out_t *out, uint32_t now_ms)
{
    if (out->swallowed) {
        return NONE;
    }
    const settings_act_t expired = settings_menu_tick(m, now_ms);
    if (out->id == BTN_ACTION && out->kind == BTN_HOLD) {
        m->confirming = false;
        return (settings_act_t){.kind = SETTINGS_ACT_EXIT, .row = m->cursor};
    }
    if (out->kind != BTN_CLICK) {
        return expired;
    }
    switch (out->id) {
    case BTN_REJECT:
        return move(m, -1);
    case BTN_APPROVE:
        return move(m, 1);
    case BTN_ACTION:
        return activate(m, now_ms);
    case BTN_COUNT:
        break;
    }
    return expired;
}

settings_act_t settings_menu_tick(settings_menu_t *m, uint32_t now_ms)
{
    if (m->confirming && (int32_t)(m->confirm_until - now_ms) <= 0) {
        m->confirming = false;
        return (settings_act_t){.kind = SETTINGS_ACT_CONFIRM_CANCELLED, .row = m->cursor};
    }
    return NONE;
}
