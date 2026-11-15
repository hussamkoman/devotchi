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
 * False parks the ui task: it stops calling lv_timer_handler() and waits for
 * true. ui_* setters still work; their changes are drawn after the restart.
 */
void ui_set_running(bool running);
bool ui_is_running(void);

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


/* ---- button tabs (design-system 5) ---- */

typedef enum {
    UI_TAB_REJECT,
    UI_TAB_ACTION,
    UI_TAB_APPROVE,
    UI_TAB_COUNT,
} ui_tab_btn_t;

typedef enum {
    UI_TAB_NONE,
    UI_TAB_CLICK,
    /** "Hold: <hold>" with the glyph in a ring. */
    UI_TAB_HOLD_ONLY,
    /** "<label>  hold <hold>": the middle tab with a click and a hold. */
    UI_TAB_CLICK_AND_HOLD,
    /** Slate glyph and word: the button is disabled (arming delay, unread pages). */
    UI_TAB_INACTIVE,
} ui_tab_state_t;

typedef struct {
    ui_tab_state_t state;
    /** The click's outcome ("Deny", "Page"). */
    const char *label;
    /** The hold's outcome ("Allow", "Settings"). */
    const char *hold;
    /** What the held tab says while the hold fills ("Allowing…"). */
    const char *holding;
    /** The commit destroys something: the APPROVE glyph and fill turn coral. */
    bool destructive;
} ui_tab_t;

typedef struct {
    ui_tab_t tab[UI_TAB_COUNT];
    /** Non-NULL: one centred tab with this text instead ("Any button: OK"); all three buttons do the same. */
    const char *any;
} ui_tabs_t;

#define UI_TAB_TEXT_SIZE  48

typedef struct {
    ui_tab_state_t state;
    /** What the tab shows, glyph aside ("Hold: Allow", "Page hold More"); "" when hidden. */
    char text[UI_TAB_TEXT_SIZE];
} ui_tab_info_t;

const char *ui_tab_state_name(ui_tab_state_t s);
const char *ui_tab_btn_name(ui_tab_btn_t b);

/** Copies the tabs on screen; returns true when the single "any button" tab shows (in out[UI_TAB_ACTION]). */
bool ui_tabs_get(ui_tab_info_t out[UI_TAB_COUNT]);

/** Hold fill of a held tab, 0..1000; 0 ends the hold and brings the other tabs back. */
void ui_tabs_hold_progress(ui_tab_btn_t btn, uint16_t permille);

/** A click on a hold-only tab: shake, and the tab says label (amber ring) for 1.5 s. */
void ui_tabs_nudge(ui_tab_btn_t btn, const char *label);

/* ---- start-up screens ---- */

/** Service name and proof-of-possession the phone app asks for, and what the attempt is doing. */
void ui_provisioning_set(const char *service_name, const char *pop, const char *status);

/** Wi-Fi setup has two steps: 0 the QR code, 1 the name and code to type. */
void ui_provisioning_set_step(uint8_t step);
uint8_t ui_provisioning_step(void);

/** Wi-Fi screen: the attempt in words, the network, failure reason and retry; can_setup offers Wi-Fi setup. */
void ui_wifi_set(const char *ssid, const char *state, const char *reason, const char *retry, bool can_setup);

/** Server screen; a retry countdown or an error turns it into the "Server unreachable" state. */
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

typedef enum {
    UI_TOAST_INFO,
    UI_TOAST_POSITIVE,
    UI_TOAST_NEGATIVE,
    UI_TOAST_ATTENTION,
    UI_TOAST_DESK,
    UI_TOAST_BATTERY,
} ui_toast_kind_t;

/** Toast over the status block for ms milliseconds (icon of kind, title, optional detail); replaces one still showing. */
void ui_toast_show(ui_toast_kind_t kind, const char *title, const char *detail, uint32_t ms);

/** ui_toast_show() of an information toast without detail. */
void ui_toast(const char *text, uint32_t ms);

/**
 * Sticky banner until ui_banner_hide(). attention: an urgent notification
 * that any button dismisses (bell, "Any button: OK" tab on the pet screen);
 * otherwise the link state (no tab: a press does not dismiss it).
 */
void ui_banner_show(const char *text, bool attention);
void ui_banner_hide(void);

/** Copies the visible toast and banner texts ("" when hidden). */
void ui_overlay_get(char toast[UI_TEXT_SIZE], char banner[UI_TEXT_SIZE]);

/** Pet screen: project header, face for the mood, status line. */
void ui_pet_set(const char *project, proto_mood_t mood, const char *status);

/** Shows the home tabs for ms milliseconds (after a press, after boot); they hide on their own. */
void ui_pet_show_tabs(uint32_t ms);

typedef enum {
    UI_PET_PAGE_PET,
    /** About 1/2: identity and network. */
    UI_PET_PAGE_ABOUT,
    /** About 2/2: diagnostics. */
    UI_PET_PAGE_ABOUT2,
    UI_PET_PAGE_COUNT,
} ui_pet_page_t;

/** Shows the face or the About page of the pet screen; the face animates only on its own page. */
void ui_pet_page_set(ui_pet_page_t page);

/** Rows of the About pages, a label and a value each; the first UI_ABOUT_PAGE1_ROWS are on page 1. */
typedef enum {
    UI_ABOUT_NAME,
    UI_ABOUT_FW,
    UI_ABOUT_DEVICE_ID,
    UI_ABOUT_SSID,
    UI_ABOUT_RSSI,
    UI_ABOUT_IP,
    UI_ABOUT_SERVER,
    UI_ABOUT_LINK,
    UI_ABOUT_UPTIME,
    UI_ABOUT_RESET,
    UI_ABOUT_ERROR,
    UI_ABOUT_NVS,
    UI_ABOUT_COUNT,
} ui_about_row_t;

#define UI_ABOUT_PAGE1_ROWS  UI_ABOUT_UPTIME

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
    /** What ACTION does on the cursor row ("Change", "Open"). */
    const char *action;
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
    /** Lines of the details after this page, for the well's footer. */
    uint16_t lines_after;
    /** Remaining time as a share of the hold time, 0..1000, and in seconds. */
    uint16_t remaining_permille;
    uint16_t remaining_s;
    /** The answer was sent: the button hints give way to "Sent...". */
    bool sent;
} ui_approval_t;

/** Approval card; the content is updated in place. */
void ui_approval_set(const ui_approval_t *a);

/** A details page: lines of the code well and their width in mono columns (design-system 3). */
#define UI_DETAILS_COLS   34
#define UI_DETAILS_LINES  3

void ui_identify_set(const char *name);

/**
 * ui demo: loads screen `name` in `variant` (NULL for the first) with constant
 * sample data, without touching the app's state; the app's ui_* calls are
 * kept but not drawn until ui_demo_off(). ESP_ERR_NOT_FOUND for an unknown
 * name or variant.
 */
esp_err_t ui_demo(const char *name, const char *variant);
/** Leaves the demo: the screen the app last asked for comes back; the app renders again after this. */
void ui_demo_off(void);
bool ui_demo_is_active(void);
/** "name[:variant|variant...] ..." for the console help. */
const char *ui_demo_usage(void);

/** Render time of the most recent frames (lv_timer_handler calls that flushed) and LVGL heap use. */
void ui_get_stats(ui_stats_t *out);
