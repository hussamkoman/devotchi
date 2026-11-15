#pragma once

#include "card_fsm.h"
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
    UI_SCREEN_QUESTION,
    UI_SCREEN_PLAN,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_PICKER,
    UI_SCREEN_IDENTIFY,
    UI_SCREEN_LOWBATT,
    /** The resting face (40-42). */
    UI_SCREEN_CLOCK,
    /** Going to sleep (91). */
    UI_SCREEN_SLEEP,
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

/** A press on an inactive tab: the tab shakes and nothing else changes. */
void ui_tabs_shake(ui_tab_btn_t btn);

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
    /** No dot: the showcase is a local demo, the link state would only distract. */
    UI_LINK_HIDDEN,
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
    /** Local time ("14:32"), "" while the clock is not set; shown where the screen has no title. */
    char time[8];
} ui_status_t;

const char *ui_batt_name(ui_batt_t b);
const char *ui_link_name(ui_link_t l);

/** Updates the status bar; objects are touched only for values that changed. */
void ui_status_set(const ui_status_t *s);
void ui_status_get(ui_status_t *out);

/** How the showcase marks the screen (ux-flows 8). */
typedef enum {
    UI_MARK_NONE,
    /** Violet 2 px frame and a `DEMO` tag before the title. */
    UI_MARK_DEMO,
    /** The same with `SAMPLE` (sample cards). */
    UI_MARK_SAMPLE,
    /** No frame; the cream `REAL REQUEST` bar in the status bar's place (85). */
    UI_MARK_REAL,
} ui_mark_t;

void ui_mark_set(ui_mark_t mark);
ui_mark_t ui_mark_get(void);
const char *ui_mark_name(ui_mark_t mark);

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

/** Receipt strip in the status bar's place for ms milliseconds (28b), with the icon and colour of kind. */
void ui_receipt_strip(ui_toast_kind_t kind, const char *text, uint32_t ms);

/** Copies the titles of the visible toast and banner and the receipt strip's text ("" when hidden). */
void ui_overlay_get(char toast[UI_TEXT_SIZE], char banner[UI_TEXT_SIZE], char strip[UI_TEXT_SIZE]);

/** Pet screen: project header, face for the mood, status line. */
void ui_pet_set(const char *project, proto_mood_t mood, const char *status);

/** Shows the home tabs for ms milliseconds (after a press, after boot); they hide on their own. */
void ui_pet_show_tabs(uint32_t ms);

typedef enum {
    UI_PET_PAGE_PET,
    /** Home page 2: recent activity. */
    UI_PET_PAGE_RECENT,
    /** About 1/2: the PCB ghost logo, the lock-up, what the device is, the version and the source link. */
    UI_PET_PAGE_ABOUT,
    /** About 2/2: credits. */
    UI_PET_PAGE_CREDITS,
    /** System info 1/2: identity and network. */
    UI_PET_PAGE_SYSTEM,
    /** System info 2/2: diagnostics. */
    UI_PET_PAGE_SYSTEM2,
    UI_PET_PAGE_COUNT,
} ui_pet_page_t;

/** Shows the face or the About page of the pet screen; the face animates only on its own page. */
void ui_pet_page_set(ui_pet_page_t page);

/** Rows of the About pages (70, 71), a label and a value each; the first UI_ABOUT_PAGE1_ROWS are on page 1. */
typedef enum {
    UI_ABOUT_FW,
    UI_ABOUT_DEVICE,
    UI_ABOUT_PET,
    UI_ABOUT_WIFI,
    UI_ABOUT_IP,
    UI_ABOUT_SERVER,
    UI_ABOUT_LINK,
    UI_ABOUT_ROOM,
    UI_ABOUT_BATTERY,
    UI_ABOUT_UPTIME,
    UI_ABOUT_RESET,
    UI_ABOUT_HEAP,
    UI_ABOUT_FRAMES,
    UI_ABOUT_REQUESTS,
    UI_ABOUT_PETS,
    UI_ABOUT_ERROR,
    UI_ABOUT_FONTS,
    UI_ABOUT_COUNT,
} ui_about_row_t;

#define UI_ABOUT_PAGE1_ROWS  UI_ABOUT_BATTERY

#define UI_RECENT_ROWS  5

/** Recent activity: up to UI_RECENT_ROWS lines, newest first, each with its time ("14:32", "5 min"). */
void ui_recent_set(const char *const times[UI_RECENT_ROWS], const char *const texts[UI_RECENT_ROWS], uint8_t n);

/**
 * Showcase moods and reactions (81, 82): bar is the status-bar title, title
 * and line replace the project and status, the tabs are Back, Next and
 * approve. NULL title ends it. Strings must outlive the call.
 */
void ui_pet_set_caption(const char *bar, const char *title, const char *line, const char *approve);

void ui_about_set(const char *const values[UI_ABOUT_COUNT]);

/** Plays a reaction on the home pet (pet_pose.h); PET_REACT_NONE ends a running one (attention). */
void ui_pet_react(pet_react_t r);

/** Lets the face animate (the power stage is active); false stops every animation timer. */
void ui_pet_set_running(bool running);

/** Frozen: no blink, bob, glance, jitter or oscillators, mood changes land at once, a reaction holds its key pose. */
void ui_pet_freeze(bool frozen);

/**
 * The pet every view draws from now on (home, picker and start-up screens
 * copy it). def is copied; NULL is ignored.
 */
void ui_pet_set_def(const pet_def_t *def);

/** Rig scale of the home pet in sixteenths (debug; 15 is the design). */
void ui_pet_set_scale(uint8_t sixteenths);

/** Outline mode of the home pet (the night clock's look). */
void ui_pet_set_outline(bool on);

typedef struct {
    char id[PET_PACK_ID_LEN + 1];
    proto_mood_t mood;
    /** The pose the mood tween is at, before a reaction. */
    pet_pose_t pose;
    uint16_t tween_permille;
    uint32_t color;
    /** Aux channels as the move drivers see them (tween, reaction and oscillators) and as the show drivers do. */
    int16_t aux[PET_PACK_AUX_COUNT];
    int16_t aux_sel[PET_PACK_AUX_COUNT];
    /** What is drawn: eye openness after blink and reaction, pupils with the glance, offsets in pixels. */
    int16_t eye_open;
    int16_t pupil_dx;
    int16_t pupil_dy;
    int16_t bob_px;
    int16_t jitter_px;
    bool blinking;
    pet_react_t reaction;
    bool frozen;
    bool outline;
    uint8_t scale;
    /** The animation timer runs (pet page on screen and the stage allows it). */
    bool animating;
    /** Draw items and how many changed in the last frame. */
    uint16_t items;
    uint16_t dirty;
    /** The view runs at 15 fps because frames missed the budget. */
    bool slow;
} ui_pet_state_t;

void ui_pet_get_state(ui_pet_state_t *out);

/** Stored pets the picker can page through (PET_STORE_SLOTS). */
#define UI_PICKER_STORED_MAX  8

/** One page of the pet picker (60-62); def NULL is the "More pets" page. Strings must outlive the call. */
typedef struct {
    const pet_def_t *def;
    const char *line;
    bool in_use;
    /** APPROVE shows "Use" (not on the pet in use). */
    bool can_use;
    uint8_t page;
    uint8_t pages;
} ui_picker_t;

/** def is copied into the picker's view. */
void ui_picker_set(const ui_picker_t *p);

#define UI_SETTINGS_ROWS        9
#define UI_SETTINGS_VALUE_SIZE  16

typedef enum {
    UI_SETTINGS_LIST,
    /** Value editor (52). */
    UI_SETTINGS_EDITOR,
    /** Destructive confirm (53). */
    UI_SETTINGS_CONFIRM,
} ui_settings_view_t;

/**
 * The list screen: Settings and its sub-lists (50, 51), the quick menu (16b)
 * and the showcase menu (80); plus the value editor and the destructive
 * confirm. Strings must outlive the call.
 */
typedef struct {
    ui_settings_view_t view;
    const char *title;
    uint8_t count;
    const char *icons[UI_SETTINGS_ROWS];
    const char *labels[UI_SETTINGS_ROWS];
    char values[UI_SETTINGS_ROWS][UI_SETTINGS_VALUE_SIZE];
    /** The row opens a list: a chevron instead of a value. */
    bool chevron[UI_SETTINGS_ROWS];
    /** The row cannot be chosen now: slate, and desc explains why while it is focused. */
    bool muted[UI_SETTINGS_ROWS];
    const char *desc;
    /** Paragraph under the list (quick menu); NULL for none. */
    const char *note;
    uint8_t cursor;
    /** APPROVE's tab on the list ("Open", "Change"). */
    const char *approve;
    /** REJECT's hold on the list ("Back", "Exit"); its click then moves up. NULL: a plain Back click. */
    const char *reject_hold;
    /* Editor: the big figure and unit, the value's place among steps, the choices line and a hint. */
    char big[UI_SETTINGS_VALUE_SIZE];
    char unit[UI_SETTINGS_VALUE_SIZE];
    uint8_t steps;
    uint8_t step;
    char choices[64];
    const char *hint;
    /* Confirm: the question, what it erases, and the hold's word. */
    const char *confirm_title;
    const char *confirm_text;
    const char *confirm_hold;
} ui_settings_t;

/** Updates the list screen in place. */
void ui_settings_set(const ui_settings_t *s);

/** Clock face (40-42). */
typedef struct {
    /** "" without a valid time: the pet only. */
    const char *time;
    const char *date;
    bool night;
} ui_clock_t;

void ui_clock_set(const ui_clock_t *c);
/** What the clock face shows; time and date are copied into the caller's buffers. */
void ui_clock_get(ui_clock_t *out, char time[8], char date[16]);

/** Going to sleep (91): seconds left, and the bar as 0..1000 of the warning still to run. */
void ui_sleep_set(uint8_t seconds, uint16_t permille);

typedef struct {
    const char *tool;
    proto_risk_t risk;
    const char *project;
    const char *sid;
    /** Claude's own description, labelled `Claude:` when words_claude; a phase-1 server's summary is not. */
    const char *words;
    bool words_claude;
    /** The server's reason for a medium or high risk; "" for none. */
    const char *reason;
    /** The wrapped lines of the command page on screen. */
    const char *details;
    /** A shell command's first page: a "$" prompt fills the two blank columns of its first line. */
    bool prompt;
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
    /** The arming delay is over: the tabs are active. */
    bool armed;
    card_allow_t allow;
    card_view_t view;
    /* The decision menu (25-27). */
    /** The command's first line, for the menu header. */
    const char *cmd_line;
    uint8_t rows;
    card_row_t row[CARD_MENU_ROWS];
    uint8_t cursor;
    card_session_t session;
    /** The session rule word for word, and whether it matches as a prefix ("pnpm test …"). */
    const char *rule;
    bool rule_prefix;
} ui_approval_t;

/** Approval card; the content is updated in place. */
void ui_approval_set(const ui_approval_t *a);


/** How an option of a question is marked. */
typedef enum {
    UI_OPT_PLAIN,
    /** The earlier choice of a single-choice question, after Back (mint check). */
    UI_OPT_CHOSEN,
    /** A multi-select option, not ticked and ticked. */
    UI_OPT_BOX,
    UI_OPT_TICKED,
} ui_opt_mark_t;

/** A question card (30-35), its read page (33), the question menu (36) or the review (37). Text is device text. */
typedef struct {
    card_view_t view;
    /** The question's header, shown in caps; NULL or "" for none. */
    const char *header;
    const char *text;
    /** 1-based position of the question and the call's question count. */
    uint8_t index;
    uint8_t count;
    uint16_t remaining_permille;
    uint16_t remaining_s;
    /** Read page: 1-based page and page count. */
    uint8_t page;
    uint8_t pages;
    /** The question had a read page: one line of it above the options. */
    bool read_first;
    uint8_t n_options;
    bool multi;
    const char *labels[PROTO_Q_OPTIONS_MAX];
    const char *descs[PROTO_Q_OPTIONS_MAX];
    ui_opt_mark_t marks[PROTO_Q_OPTIONS_MAX];
    /** Multi-select: options ticked, for the send row. */
    uint8_t ticked;
    /** Focused row: an option, or n_options for the send row. */
    uint8_t opt;
    /** Focused row of the question menu. */
    uint8_t cursor;
    /** Review: one well per question, its header (or "Question n") and the answer in words. */
    uint8_t answers;
    const char *review_label[PROTO_Q_MAX];
    const char *review_answer[PROTO_Q_MAX];
    bool armed;
    bool sent;
} ui_question_t;

void ui_question_set(const ui_question_t *q);

/** Lines text takes in the body font across the card, for card_fsm_open_ask(). */
uint16_t ui_question_lines(const char *text);

/** The plan card (29) or the plan menu (29b). Text is device text. */
typedef struct {
    card_view_t view;
    const char *title;
    const char (*steps)[PROTO_PLAN_STEP_SIZE];
    uint8_t n_steps;
    /** 1-based page of CARD_PLAN_STEPS steps, and the page count. */
    uint8_t page;
    uint8_t pages;
    uint16_t more;
    bool cut;
    const char *project;
    uint16_t remaining_permille;
    uint16_t remaining_s;
    card_allow_t approve;
    /** The "Approve, accept all edits" row. */
    card_session_t edits;
    uint8_t rows;
    card_row_t row[CARD_MENU_ROWS];
    uint8_t cursor;
    bool armed;
    bool sent;
} ui_plan_t;

void ui_plan_set(const ui_plan_t *p);

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
/** True while ui demo has the screen; takes ui_lock (inside the lock use ui_demo_active_locked). */
bool ui_demo_is_active(void);
/** "name[:variant|variant...] ..." for the console help. */
const char *ui_demo_usage(void);

/** Render time of the most recent frames (lv_timer_handler calls that flushed) and LVGL heap use. */
void ui_get_stats(ui_stats_t *out);
