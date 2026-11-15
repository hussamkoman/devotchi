#pragma once

#include "btn_fsm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ux-flows 6: how long each settings view waits for a press. */
#define SETTINGS_LIST_IDLE_MS     60000u
#define SETTINGS_EDITOR_IDLE_MS   30000u
#define SETTINGS_CONFIRM_IDLE_MS  10000u

#define SETTINGS_VALUE_TEXT_SIZE  16
#define SETTINGS_LIST_MAX         9

/** Every row of the tree (ux-flows 1); a row shows in one or more lists. */
typedef enum {
    SETTINGS_ITEM_PET,
    SETTINGS_ITEM_DISPLAY,
    SETTINGS_ITEM_CLOCK,
    SETTINGS_ITEM_HAPTICS_LED,
    SETTINGS_ITEM_DESK,
    SETTINGS_ITEM_NETWORK,
    SETTINGS_ITEM_SHOWCASE,
    SETTINGS_ITEM_SYSTEM,
    SETTINGS_ITEM_ABOUT,
    /* Value rows, edited in the value editor (52); see SETTINGS_VALUE_FIRST. */
    SETTINGS_ITEM_BRIGHTNESS,
    SETTINGS_ITEM_SCREEN_TIMEOUT,
    SETTINGS_ITEM_DEEP_SLEEP,
    SETTINGS_ITEM_SHOW_CLOCK,
    SETTINGS_ITEM_CLOCK_BRIGHTNESS,
    SETTINGS_ITEM_H24,
    SETTINGS_ITEM_NIGHT,
    SETTINGS_ITEM_HAPTICS,
    SETTINGS_ITEM_LED,
    SETTINGS_ITEM_CLICKS,
    SETTINGS_ITEM_WIFI_SETUP,
    SETTINGS_ITEM_UNPAIR,
    SETTINGS_ITEM_FACTORY_RESET,
    /** The quick menu's way into the top list. */
    SETTINGS_ITEM_SETTINGS,
    SETTINGS_ITEM_COUNT,
} settings_item_t;

#define SETTINGS_VALUE_FIRST  SETTINGS_ITEM_BRIGHTNESS
#define SETTINGS_VALUE_COUNT  (SETTINGS_ITEM_CLICKS - SETTINGS_ITEM_BRIGHTNESS + 1)

/** Show clock values (`s_clock`, architecture 7.3). */
#define SETTINGS_CLOCK_RESTING     0
#define SETTINGS_CLOCK_ALWAYS_USB  1
#define SETTINGS_CLOCK_NEVER       2

typedef enum {
    /** Quick menu (16b): Settings, Showcase, Desk mode. */
    SETTINGS_LIST_QUICK,
    /** Top list (50). */
    SETTINGS_LIST_TOP,
    SETTINGS_LIST_DISPLAY,
    SETTINGS_LIST_CLOCK,
    SETTINGS_LIST_HAPTICS,
    SETTINGS_LIST_NETWORK,
    SETTINGS_LIST_COUNT,
} settings_list_t;

typedef enum {
    SETTINGS_VIEW_LIST,
    /** Value editor (52): the value on screen is previewed, not saved. */
    SETTINGS_VIEW_EDITOR,
    /** Destructive confirm (53): APPROVE commit hold. */
    SETTINGS_VIEW_CONFIRM,
} settings_view_t;

typedef enum {
    SETTINGS_KIND_LIST,
    SETTINGS_KIND_VALUE,
    /** Desk mode: APPROVE toggles it in place. */
    SETTINGS_KIND_TOGGLE,
    /** Opens something outside the menu (picker, showcase, About, Wi-Fi setup). */
    SETTINGS_KIND_ACTION,
    /** Needs the confirm view first. */
    SETTINGS_KIND_DESTRUCTIVE,
} settings_kind_t;

typedef enum {
    SETTINGS_ACT_NONE,
    /** Cursor moved, a list opened or closed, the editor or confirm opened. */
    SETTINGS_ACT_MOVED,
    /** The editor stepped: apply value to the device without saving it. */
    SETTINGS_ACT_PREVIEW,
    /** Save value and keep it applied. */
    SETTINGS_ACT_SAVE,
    /** The edit was dropped (REJECT or timeout): apply value, the one stored before. */
    SETTINGS_ACT_CANCEL,
    /** Desk mode should become value (1 on, 0 off). */
    SETTINGS_ACT_DESK,
    /** A commit hold on the confirm view moves: value is permille (0 ends it). */
    SETTINGS_ACT_PROGRESS,
    /** APPROVE released early on the confirm view: explain the hold. */
    SETTINGS_ACT_NUDGE,
    /** The row cannot be chosen now (Showcase while a request waits). */
    SETTINGS_ACT_REFUSED,
    /** Carry out item (an action row, or a destructive row after its hold). */
    SETTINGS_ACT_EXECUTE,
    /** Leave the menu. */
    SETTINGS_ACT_EXIT,
} settings_act_kind_t;

typedef struct {
    settings_act_kind_t kind;
    settings_item_t item;
    uint16_t value;
} settings_act_t;

typedef struct {
    uint16_t values[SETTINGS_VALUE_COUNT];
    bool desk;
    /** Showcase rows are inactive ("A request is waiting", protocol 14.7). */
    bool showcase_blocked;
    settings_list_t list;
    uint8_t cursor;
    settings_view_t view;
    /** The value the editor started from (restored on cancel). */
    uint16_t edit_old;
    /** Last press or opening; the view's timeout runs from it. */
    uint32_t last_ms;
} settings_menu_t;

/** Opens list on its first row; values are indexed by item - SETTINGS_VALUE_FIRST. */
void settings_menu_open(settings_menu_t *m, settings_list_t list, const uint16_t values[SETTINGS_VALUE_COUNT],
                        bool desk, uint32_t now_ms);

/**
 * Opens the view a console path names: "top", "quick", a list key
 * ("display"), "<list>/<value item>" (editor) or "<list>/<destructive item>"
 * (confirm). False, and m unchanged, for an unknown path.
 */
bool settings_menu_open_path(settings_menu_t *m, const char *path, const uint16_t values[SETTINGS_VALUE_COUNT],
                             bool desk, uint32_t now_ms);

/** Values the menu does not own: desk mode (the server decides) and the showcase guard. */
void settings_menu_sync(settings_menu_t *m, bool desk, bool showcase_blocked);

settings_act_t settings_menu_button(settings_menu_t *m, const btn_fsm_out_t *out, uint32_t now_ms);

/** The view's timeout: an editor cancels, a confirm returns to its list, a list exits. */
settings_act_t settings_menu_tick(settings_menu_t *m, uint32_t now_ms);

/** When settings_menu_tick() has something to do. */
uint32_t settings_menu_deadline(const settings_menu_t *m);

btn_mode_t settings_menu_btn_mode(const settings_menu_t *m, btn_id_t id);

/** Rows of list in screen order. */
const settings_item_t *settings_list_items(settings_list_t list, size_t *count);
const char *settings_list_title(settings_list_t list);
/** True for a list without a parent (top, quick): holding Back there leaves Settings. */
bool settings_list_is_root(settings_list_t list);
const char *settings_list_key(settings_list_t list);

settings_item_t settings_menu_item(const settings_menu_t *m);
/** Current value of a value item (the previewed one while it is edited). */
uint16_t settings_menu_value(const settings_menu_t *m, settings_item_t item);

settings_kind_t settings_item_kind(settings_item_t item);
const char *settings_item_label(settings_item_t item);
/** Short machine name ("brightness", "factory_reset", ...). */
const char *settings_item_key(settings_item_t item);
/** The list a SETTINGS_KIND_LIST item opens. */
settings_list_t settings_item_list(settings_item_t item);

/** Allowed values of a value item in editor order; NULL for other items. */
const uint16_t *settings_item_choices(settings_item_t item, size_t *count);
bool settings_item_allowed(settings_item_t item, uint16_t value);

/** The value as the editor's big figure and its unit ("75" and "%", "Gentle" and ""). */
void settings_value_parts(settings_item_t item, uint16_t value, char num[SETTINGS_VALUE_TEXT_SIZE],
                          char unit[SETTINGS_VALUE_TEXT_SIZE]);
/** The value as a list row shows it ("75 %", "Never"). */
void settings_value_text(settings_item_t item, uint16_t value, char out[SETTINGS_VALUE_TEXT_SIZE]);

const char *settings_view_name(settings_view_t view);
