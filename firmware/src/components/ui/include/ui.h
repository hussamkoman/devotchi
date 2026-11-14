#pragma once

#include "pet_pose.h"
#include "proto.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_TEXT_SIZE     96
#define UI_MAX_TEXTS     24
/** What ui_get_texts() reports in place of a secret shown on screen. */
#define UI_TEXT_HIDDEN   "<hidden>"

typedef enum {
    UI_SCREEN_SPLASH,
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

typedef enum {
    UI_BATT_ABSENT,
    UI_BATT_DISCHARGING,
    UI_BATT_CHARGING,
    UI_BATT_FULL,
} ui_batt_t;

typedef enum {
    UI_LINK_REGISTERED,
    UI_LINK_CONNECTING,
    UI_LINK_OFFLINE,
} ui_link_t;

typedef struct {
    ui_batt_t battery;
    /** 0..100; ignored when the battery is absent. */
    uint8_t pct;
    bool low;
    bool desk;
    /** 1..4, or 0 when WiFi is down (bars crossed out). */
    uint8_t rssi_bars;
    ui_link_t link;
} ui_status_t;

const char *ui_batt_name(ui_batt_t b);
const char *ui_link_name(ui_link_t l);

/** Updates the status bar; objects are touched only for values that changed. */
void ui_status_set(const ui_status_t *s);
void ui_status_get(ui_status_t *out);

#define UI_TOAST_DEFAULT_MS  2500

/** One line at the bottom of the screen for ms milliseconds; replaces a toast still showing. */
void ui_toast(const char *text, uint32_t ms);

/** Sticky one-line banner below the status bar until ui_banner_hide(). */
void ui_banner_show(const char *text);
void ui_banner_hide(void);

/** Copies the visible toast and banner texts ("" when hidden). */
void ui_overlay_get(char toast[UI_TEXT_SIZE], char banner[UI_TEXT_SIZE]);

/** Pet screen: project header, face for the mood, status line. */
void ui_pet_set(const char *project, proto_mood_t mood, const char *status);

typedef enum {
    UI_PET_PAGE_PET,
    UI_PET_PAGE_ABOUT,
    UI_PET_PAGE_COUNT,
} ui_pet_page_t;

/** Shows the face or the About page of the pet screen; the face animates only on its own page. */
void ui_pet_page_set(ui_pet_page_t page);

/** Rows of the About page; each is shown as "<label>: <value>". */
typedef enum {
    UI_ABOUT_NAME,
    UI_ABOUT_FW,
    UI_ABOUT_DEVICE_ID,
    UI_ABOUT_UPTIME,
    UI_ABOUT_RESET,
    UI_ABOUT_SSID,
    UI_ABOUT_IP,
    UI_ABOUT_RSSI,
    UI_ABOUT_SERVER,
    UI_ABOUT_LINK,
    UI_ABOUT_ERROR,
    UI_ABOUT_NVS,
    UI_ABOUT_COUNT,
} ui_about_row_t;

void ui_about_set(const char *const values[UI_ABOUT_COUNT]);

typedef enum {
    UI_PET_REACT_NONE,
    /** Happy arc eyes for 1.5 s. */
    UI_PET_REACT_PAT,
    /** Squish and squint for 600 ms. */
    UI_PET_REACT_POKE,
    /** Spiral eyes for 2 s. */
    UI_PET_REACT_DIZZY,
} ui_pet_reaction_t;

const char *ui_pet_reaction_name(ui_pet_reaction_t r);

void ui_pet_react(ui_pet_reaction_t r);

/** Lets the face animate (the power stage is active); false stops every animation timer. */
void ui_pet_set_running(bool running);

/** Frozen: no blink, bob, glance or jitter, and mood changes land at once (reproducible screenshots). */
void ui_pet_freeze(bool frozen);

typedef struct {
    proto_mood_t mood;
    /** The pose the mood tween is at. */
    pet_pose_t pose;
    uint16_t tween_permille;
    /** What is drawn: eye openness after blink and reaction, pupils with the glance, offsets in pixels. */
    int16_t eye_open;
    int16_t pupil_dx;
    int16_t pupil_dy;
    int16_t bob_px;
    int16_t jitter_px;
    bool blinking;
    ui_pet_reaction_t reaction;
    bool frozen;
    /** The animation timer runs (pet page on screen and the stage allows it). */
    bool animating;
} ui_pet_state_t;

void ui_pet_get_state(ui_pet_state_t *out);

#define UI_SETTINGS_ROWS        10
#define UI_SETTINGS_VALUE_SIZE  16

typedef struct {
    const char *labels[UI_SETTINGS_ROWS];
    char values[UI_SETTINGS_ROWS][UI_SETTINGS_VALUE_SIZE];
    uint8_t cursor;
    /** The cursor row asks for the second ACTION click. */
    bool confirming;
} ui_settings_t;

/** Settings list; rows are restyled only when the cursor or the confirmation changed. */
void ui_settings_set(const ui_settings_t *s);

typedef struct {
    const char *tool;
    proto_risk_t risk;
    const char *project;
    const char *summary;
    /** The wrapped lines of the details page on screen. */
    const char *details;
    /** 1-based queue position of the request on screen and the queue length. */
    uint8_t position;
    uint8_t count;
    /** 1-based details page and the page count; pages 0 hides the details area. */
    uint8_t page;
    uint8_t pages;
    /** Remaining time as a share of the hold time, 0..1000. */
    uint16_t remaining_permille;
    /** The answer was sent: the button hints give way to "Sent...". */
    bool sent;
} ui_approval_t;

/** Approval card; the content is updated in place. */
void ui_approval_set(const ui_approval_t *a);

#define UI_DETAILS_COLS   38
#define UI_DETAILS_LINES  6

void ui_identify_set(const char *name);

/** Render time of the most recent frames (lv_timer_handler calls that flushed) and LVGL heap use. */
void ui_get_stats(ui_stats_t *out);
