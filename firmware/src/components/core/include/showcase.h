#pragma once

#include "btn_fsm.h"

#include <stdbool.h>
#include <stdint.h>

/* ux-flows 6 and 8. */
#define SHOWCASE_IDLE_MS   300000u
#define SHOWCASE_GUARD_MS  10000u
/** Arming delay of a real request shown during the showcase or its guard. */
#define SHOWCASE_ARM_MS    1500u
#define SHOWCASE_MOODS     11
#define SHOWCASE_REACTIONS 5

typedef enum {
    SHOWCASE_PAGE_MENU,
    SHOWCASE_PAGE_MOODS,
    SHOWCASE_PAGE_REACTIONS,
    SHOWCASE_PAGE_PERMISSION,
    SHOWCASE_PAGE_QUESTION,
    SHOWCASE_PAGE_CLOCK,
    /** The sample notification: a toast and the pet's reaction. */
    SHOWCASE_PAGE_NOTIFY,
    SHOWCASE_PAGE_COUNT,
} showcase_page_t;

/** Rows of the showcase menu (80). */
typedef enum {
    SHOWCASE_ROW_MOODS,
    SHOWCASE_ROW_REACTIONS,
    SHOWCASE_ROW_PERMISSION,
    SHOWCASE_ROW_QUESTION,
    SHOWCASE_ROW_NOTIFY,
    SHOWCASE_ROW_CLOCK,
    SHOWCASE_ROW_LEAVE,
    SHOWCASE_ROW_COUNT,
} showcase_row_t;

typedef enum {
    SHOWCASE_ACT_NONE,
    /** Menu cursor, mood or reaction stepped. */
    SHOWCASE_ACT_MOVED,
    SHOWCASE_ACT_OPEN,
    SHOWCASE_ACT_BACK,
    /** The sample notification page opened: play the toast and the reaction. */
    SHOWCASE_ACT_NOTIFY,
    /** Pat on the moods page, replay on the reactions and notification pages. */
    SHOWCASE_ACT_PLAY,
    /** The clock face switched between day and night. */
    SHOWCASE_ACT_NIGHT,
    /** The press belongs to the sample card. */
    SHOWCASE_ACT_CARD,
    SHOWCASE_ACT_LEAVE,
} showcase_act_kind_t;

typedef struct {
    showcase_act_kind_t kind;
    showcase_page_t page;
} showcase_act_t;

typedef struct {
    bool active;
    showcase_page_t page;
    showcase_row_t cursor;
    uint8_t mood;
    uint8_t reaction;
    bool night;
    uint32_t last_press_ms;
    /** A real request shown before this is guarded (85); valid while guard is set. */
    bool guard;
    uint32_t guard_until;
} showcase_t;

void showcase_init(showcase_t *s);

/** Protocol 14.7: never while a real request is queued. */
bool showcase_may_enter(uint8_t pending);

/** Starts on the menu; false (and nothing changes) while pending > 0. */
bool showcase_enter(showcase_t *s, uint8_t pending, uint32_t now_ms);

/** Ends the mode and starts the 10 s guard. */
void showcase_leave(showcase_t *s, uint32_t now_ms);

/** Back to the menu (a sample card finished). */
void showcase_back(showcase_t *s);

/** Opens page directly (console). */
void showcase_open(showcase_t *s, showcase_page_t page, uint32_t now_ms);

showcase_act_t showcase_button(showcase_t *s, const btn_fsm_out_t *out, uint32_t now_ms);

/** LEAVE after SHOWCASE_IDLE_MS without a press. */
showcase_act_t showcase_tick(showcase_t *s, uint32_t now_ms);

/** True while a real request must be guarded: in the showcase or within 10 s after it. */
bool showcase_guarded(showcase_t *s, uint32_t now_ms);

/** Press modes on the showcase's own pages; card pages use the card's modes except REJECT. */
btn_mode_t showcase_btn_mode(const showcase_t *s, btn_id_t id);

const char *showcase_page_name(showcase_page_t page);
bool showcase_page_parse(const char *name, showcase_page_t *out);
