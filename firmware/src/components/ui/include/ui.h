#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_DIAG_LINES    6
#define UI_TEXT_SIZE     96
#define UI_MAX_TEXTS     24
/** What ui_get_texts() reports in place of a secret shown on screen. */
#define UI_TEXT_HIDDEN   "<hidden>"

typedef enum {
    UI_SCREEN_SPLASH,
    UI_SCREEN_DIAG,
    UI_SCREEN_PROVISIONING,
    UI_SCREEN_WIFI,
    UI_SCREEN_CONNECTING,
    UI_SCREEN_PAIRING,
    UI_SCREEN_PET,
    UI_SCREEN_APPROVAL,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_IDENTIFY,
    UI_SCREEN_LOWBATT,
    UI_SCREEN_COUNT,
} ui_screen_id_t;

typedef struct {
    uint32_t frames;
    uint32_t avg_ms_x10;
    uint32_t max_ms_x10;
    uint8_t lvgl_used_pct;
} ui_stats_t;

/** Creates the screens and loads the splash screen; display_init() must have run. */
esp_err_t ui_init(void);

/** Starts the task that runs lv_timer_handler(). */
esp_err_t ui_start(const char *task_name, uint32_t stack_size, uint32_t priority);

/**
 * Takes the recursive lock that guards every lv_* and esp_lcd panel call;
 * returns false if it stayed busy for a second. Needed only by code that
 * calls LVGL itself: the ui_* functions lock on their own.
 */
bool ui_lock(void);
void ui_unlock(void);

const char *ui_screen_name(ui_screen_id_t id);

/** Loads a screen; ESP_ERR_NOT_SUPPORTED for an id without an implementation. */
esp_err_t ui_show_screen(ui_screen_id_t id);
ui_screen_id_t ui_current_screen(void);
const char *ui_current_screen_name(void);

/** Sets one text line of the diag screen; the text is reduced to printable ASCII. */
void ui_diag_set_line(size_t line, const char *text);

/** Service name and proof-of-possession the phone app asks for, and what the attempt is doing. */
void ui_provisioning_set(const char *service_name, const char *pop, const char *status);

/** WiFi screen lines; an empty string hides nothing but leaves the line blank. */
void ui_wifi_set(const char *ssid, const char *state, const char *reason, const char *retry, const char *hint);

void ui_connecting_set(const char *host, const char *state, const char *error, const char *retry);

/**
 * code is the six-character pairing code ("" while none is known);
 * remaining_s of total_s drives the expiry bar.
 */
void ui_pairing_set(const char *code, const char *host, const char *error, uint32_t remaining_s, uint32_t total_s);

/**
 * Copies the text of every visible label on the current screen and the top
 * layer, in creation order. Returns the number of texts.
 */
size_t ui_get_texts(char texts[][UI_TEXT_SIZE], size_t max_texts);

/** Render time of the most recent frames (lv_timer_handler calls that flushed) and LVGL heap use. */
void ui_get_stats(ui_stats_t *out);
