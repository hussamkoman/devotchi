#pragma once

#include "btn_fsm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** How long a confirmation row waits for the second ACTION click. */
#define SETTINGS_CONFIRM_MS     5000
#define SETTINGS_VALUE_TEXT_SIZE 16

typedef enum {
    SETTINGS_ROW_BRIGHTNESS,
    SETTINGS_ROW_SCREEN_TIMEOUT,
    SETTINGS_ROW_DEEP_SLEEP,
    SETTINGS_ROW_HAPTICS,
    SETTINGS_ROW_LED,
    SETTINGS_ROW_MODE,
    /** Opens the pet picker (60-62). */
    SETTINGS_ROW_PET,
    SETTINGS_ROW_WIFI_SETUP,
    SETTINGS_ROW_UNPAIR,
    SETTINGS_ROW_FACTORY_RESET,
    SETTINGS_ROW_ABOUT,
    SETTINGS_ROW_COUNT,
} settings_row_t;

/** Rows before this one carry a value; the rest are actions. */
#define SETTINGS_VALUE_ROWS  SETTINGS_ROW_PET

/** Mode row values. */
#define SETTINGS_MODE_ASK   0
#define SETTINGS_MODE_DESK  1

typedef enum {
    SETTINGS_ACT_NONE,
    SETTINGS_ACT_MOVED,
    /** row's value changed to value: apply and persist it. */
    SETTINGS_ACT_VALUE,
    /** row now asks for the second click. */
    SETTINGS_ACT_CONFIRM_ASK,
    /** The confirmation was dropped (cursor moved or timeout). */
    SETTINGS_ACT_CONFIRM_CANCELLED,
    /** Carry out row (WiFi setup, Unpair or Factory reset after its confirmation, or About). */
    SETTINGS_ACT_EXECUTE,
    /** Leave the settings screen. */
    SETTINGS_ACT_EXIT,
} settings_act_kind_t;

typedef struct {
    settings_act_kind_t kind;
    settings_row_t row;
    uint16_t value;
} settings_act_t;

typedef struct {
    uint16_t values[SETTINGS_VALUE_ROWS];
    settings_row_t cursor;
    bool confirming;
    uint32_t confirm_until;
} settings_menu_t;

/** Opens the menu on the first row with the given values (indexed by row). */
void settings_menu_open(settings_menu_t *m, const uint16_t values[SETTINGS_VALUE_ROWS]);

/** Replaces a value the menu does not own (the room mode, which the server decides). */
void settings_menu_sync(settings_menu_t *m, settings_row_t row, uint16_t value);

/**
 * Applies one button output: REJECT click up, APPROVE click down, ACTION
 * click change or activate, ACTION long exit. Swallowed outputs and long
 * presses of the other buttons do nothing.
 */
settings_act_t settings_menu_button(settings_menu_t *m, const btn_fsm_out_t *out, uint32_t now_ms);

/** Drops a confirmation whose time ran out. */
settings_act_t settings_menu_tick(settings_menu_t *m, uint32_t now_ms);

/** True when row needs the second ACTION click. */
bool settings_menu_needs_confirm(settings_row_t row);

/** Allowed values of a value row in the order the menu cycles through them; NULL for an action row. */
const uint16_t *settings_menu_choices(settings_row_t row, size_t *count);

/** True when value is one of row's choices. */
bool settings_menu_allowed(settings_row_t row, uint16_t value);

const char *settings_menu_label(settings_row_t row);

/** Short machine name of the row ("brightness", "unpair", ...). */
const char *settings_menu_key(settings_row_t row);

/** Writes the value as shown ("75 %", "30 s", "Never", "Gentle", ...); "" for action rows. */
void settings_menu_value_text(settings_row_t row, uint16_t value, char out[SETTINGS_VALUE_TEXT_SIZE]);
