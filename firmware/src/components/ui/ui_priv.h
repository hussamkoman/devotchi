#pragma once

#include "proto.h"
#include "ui.h"

#include "lvgl.h"

#include <stdbool.h>

/* Each returns its screen object; called once from ui_init() with the lock held. */
lv_obj_t *screen_splash_create(void);
lv_obj_t *screen_provisioning_create(void);
lv_obj_t *screen_wifi_create(void);
lv_obj_t *screen_connecting_create(void);
lv_obj_t *screen_pairing_create(void);
lv_obj_t *screen_pet_create(void);
lv_obj_t *screen_approval_create(void);
lv_obj_t *screen_identify_create(void);
lv_obj_t *screen_settings_create(void);
lv_obj_t *screen_lowbatt_create(void);

/** Caller holds the ui lock; tells the pet screen whether it is the loaded screen. */
void screen_pet_set_shown(bool shown);

/** An attention banner is up: the pet screen shows the single "Any button: OK" tab. */
void screen_pet_set_attention(bool on);

/*
 * Demo content (ui demo): each loads constant sample data into its screen
 * through the same code the ui_* setters use; the ui lock is held. The OFF
 * values reset what the demo changed beyond the data the app sets again.
 */
typedef enum {
    PET_DEMO_OFF = -1,
    PET_DEMO_IDLE,
    PET_DEMO_WORKING,
    PET_DEMO_ANGRY,
    /** Idle with the home tabs. */
    PET_DEMO_TABS,
    /** Pet without project and status line, for a toast over the status block. */
    PET_DEMO_HAPPY_BARE,
    PET_DEMO_SAD_BARE,
    PET_DEMO_IDLE_BARE,
    PET_DEMO_ABOUT1,
    PET_DEMO_ABOUT2,
} ui_pet_demo_t;

/* `approval` variants (20-24, 28b), then `menu` (25-27 and the locked session row), then `tabs`. */
typedef enum {
    APPROVAL_DEMO_OFF = -1,
    APPROVAL_DEMO_LOW,
    APPROVAL_DEMO_MED,
    APPROVAL_DEMO_DETAILS,
    APPROVAL_DEMO_LONG,
    APPROVAL_DEMO_HIGH,
    APPROVAL_DEMO_CLICKED,
    APPROVAL_DEMO_HOLDING,
    APPROVAL_DEMO_SENT,
    APPROVAL_DEMO_NEXT,
    APPROVAL_DEMO_MENU,
    APPROVAL_DEMO_MENU_SESSION,
    APPROVAL_DEMO_MENU_HIGH,
    APPROVAL_DEMO_MENU_LOCKED,
    /** High-risk card with its tabs holding, nudged, as one "any" tab, inactive; these stay last. */
    APPROVAL_DEMO_HOLD,
    APPROVAL_DEMO_NUDGE,
    APPROVAL_DEMO_ANY,
    APPROVAL_DEMO_INACTIVE,
} ui_approval_demo_t;

typedef enum {
    W_TOAST_DEMO_NONE = -1,
    W_TOAST_DEMO_POSITIVE,
    W_TOAST_DEMO_STICKY,
    W_TOAST_DEMO_DESK,
    W_TOAST_DEMO_BATTERY,
    W_TOAST_DEMO_STRIP,
    /** Receipt toast on home (28). */
    W_TOAST_DEMO_RECEIPT,
} w_toast_demo_t;

/** Variant 0: QR step, 1: code step; -1 returns to the QR step. */
void screen_provisioning_demo(int variant);
void screen_wifi_demo(void);
void screen_connecting_demo(bool offline);
void screen_pairing_demo(void);
void screen_pet_demo(ui_pet_demo_t demo);
void screen_approval_demo(int demo);
void screen_settings_demo(void);
void screen_identify_demo(void);
/** Draws st instead of the app's status; NULL draws the app's again. */
void w_status_demo(const ui_status_t *st);
/** Shows a sample toast or sticky banner; NONE hides it and restores the app's banner. */
void w_toast_demo(w_toast_demo_t demo);

/*
 * Screen manager (ui.c). Each screen keeps its button tabs and its status-bar
 * title here; whichever screen is loaded has them applied. The strings must
 * outlive the call (string literals or the screen's own static buffers).
 * Caller holds the ui lock.
 */
void ui_screen_set_tabs(ui_screen_id_t id, const ui_tabs_t *tabs);
void ui_screen_set_title(ui_screen_id_t id, const char *title);
/** True while ui demo has the screen: the app's setters are ignored. */
bool ui_demo_active(void);

/* The animated face; every call below needs the ui lock. */
void pet_view_create(lv_obj_t *parent);
void pet_view_set_mood(proto_mood_t mood);
/** On screen: the pet screen is loaded and shows its face page; the face is hidden otherwise. */
void pet_view_set_visible(bool visible);
/** The power stage allows animation. */
void pet_view_set_powered(bool powered);
void pet_view_set_frozen(bool frozen);
void pet_view_react(ui_pet_reaction_t r);
void pet_view_get_state(ui_pet_state_t *out);

/** Marks a label whose text ui_get_texts() must not reveal (the provisioning proof-of-possession). */
#define UI_FLAG_SECRET  LV_OBJ_FLAG_USER_1
/** Marks an object whose labels ui_get_texts() skips because the screen command reports them separately. */
#define UI_FLAG_NO_TEXT LV_OBJ_FLAG_USER_2

/** Longest label text ui_label_set() keeps (a details page and its line breaks, in UTF-8). */
#define UI_LABEL_TEXT_SIZE  320

/** Screen object with the theme background and no scrolling. */
lv_obj_t *ui_screen_base_create(void);

/** Plain object without theme styling, scrolling or click handling. */
lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/** Filled rectangle of one colour. */
lv_obj_t *ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color, int32_t radius);

/**
 * Label of width w at (x, y); w 0 sizes it to its text. one_line ends long
 * text in an ellipsis instead of wrapping.
 */
lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t x, int32_t y, int32_t w,
                  bool one_line);

/** ui_text() at the bare-text margin across the screen width, aligned as given. */
lv_obj_t *ui_text_line(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t y, lv_text_align_t align);

/** Icon label (ui_icons.h glyph) in color. */
lv_obj_t *ui_icon(lv_obj_t *parent, const char *icon, lv_color_t color, int32_t x, int32_t y);

/**
 * Sets text that may come from outside (text_sanitize_device()) and updates
 * the label only when it changed. Caller holds the ui lock.
 */
void ui_label_set(lv_obj_t *label, const char *text);

/**
 * The host of a server URL for a mono line of at most cols columns: no
 * scheme, no path, no ":8080" (the default port of a Devotchi server), and a
 * host that is still too long cut in the middle.
 */
void ui_host_text(char *dst, size_t cap, const char *url, size_t cols);

/** ui_label_set() of the upper-cased text, for labels in the caps style. */
void ui_label_set_caps(lv_obj_t *label, const char *text);

/** Changes the hidden flag only when it differs. */
void ui_set_hidden(lv_obj_t *obj, bool hidden);

/* ---- widgets (ui/widgets) ---- */

/* Status bar on the top layer (design-system 4). */
void w_status_create(void);
void w_status_set_visible(bool visible);
/** Page title in caps on the left; "" leaves the time slot, which stays empty until the clock is known. */
void w_status_set_title(const char *title);

/* Button tabs on the top layer (design-system 5). */
void w_tabs_create(void);
/** NULL or a table without tabs hides them; a table that brings tabs back makes them rise. */
void w_tabs_set(const ui_tabs_t *tabs);

/* Toast, sticky banner and receipt strip on the top layer. */
void w_toast_create(void);

/** Receipt strip in the status bar's place for ms milliseconds (0: until replaced). */
void w_toast_strip(const char *icon, lv_color_t color, const char *text, const char *right, uint32_t ms);

typedef struct w_list w_list_t;

typedef struct {
    const char *icon;
    const char *label;
    /** Right-aligned value; NULL for none. */
    const char *value;
    /** One mono line above the description, shown only while the row is focused; NULL for none. */
    const char *code;
    /** Up to three lines of description, shown only while the row is focused; NULL for none. */
    const char *desc;
    /** Drawn in slate: the row exists but cannot be chosen yet. */
    bool muted;
} w_list_row_t;

typedef enum {
    W_LIST_FOCUS_CREAM,
    /** A row that grants more than one call. */
    W_LIST_FOCUS_AMBER,
    /** A row that destroys something. */
    W_LIST_FOCUS_CORAL,
    /** A muted row: a dark bar, the text stays light. */
    W_LIST_FOCUS_MUTED,
} w_list_focus_t;

#define W_LIST_MAX_ROWS  12

/** List in the content band: rows of row_h + 2 px, a moving focus bar and a scroll thumb when rows overflow h. */
w_list_t *w_list_create(lv_obj_t *parent, int32_t y, int32_t h, int32_t row_h);
void w_list_set(w_list_t *l, const w_list_row_t *rows, uint8_t count, uint8_t focus, w_list_focus_t style);

typedef struct w_well w_well_t;

/** Code well at the surface margin: mono text, optional footer (what is left, page). */
w_well_t *w_well_create(lv_obj_t *parent, int32_t y);

/**
 * Sets up to `lines` lines of text (already wrapped) and the footer texts
 * (NULL hides the footer). edge draws the high-risk edge in edge_color.
 * Returns the well's height.
 */
int32_t w_well_set(w_well_t *w, const char *text, uint8_t lines, const char *foot_left, const char *foot_right,
                   bool edge, lv_color_t edge_color);
lv_obj_t *w_well_obj(w_well_t *w);
/** A slate "$" over the first two columns of the first line (the text leaves them blank). */
void w_well_set_prompt(w_well_t *w, bool on);

typedef struct w_countdown w_countdown_t;

/** Countdown bar across the surface width at y; with_seconds adds the "24 s" label right above it. */
w_countdown_t *w_countdown_create(lv_obj_t *parent, int32_t y, bool with_seconds);
/** Moves in 1 px steps; objects change only when the width, colour or seconds changed. */
void w_countdown_set(w_countdown_t *c, uint16_t permille, int32_t seconds, lv_color_t color);

typedef struct w_chip w_chip_t;

w_chip_t *w_chip_create(lv_obj_t *parent, int32_t x, int32_t y);
/** Pill in color with icon and text; bars 1..3 adds the risk bars and words after it (0: none). */
void w_chip_set(w_chip_t *c, const char *icon, const char *text, lv_color_t color, uint8_t bars, const char *words);
