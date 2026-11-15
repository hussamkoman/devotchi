#pragma once

#include "btn_fsm.h"
#include "reqq.h"

#include <stdbool.h>
#include <stdint.h>

/* The code well (design-system 3, ux-flows 3.1): mono columns and lines per page. */
#define CARD_COLS        34
#define CARD_LINES       3
/** A command with more pages than this has no Allow on the device (read before yes). */
#define CARD_MAX_PAGES   4
/** Arming delays (ux-flows 6): a new card, menu or return to the card; a card that replaces another. */
#define CARD_ARM_MS      500
#define CARD_ARM_NEXT_MS 1000
#define CARD_MENU_ROWS   4

typedef enum {
    CARD_VIEW_CARD,
    CARD_VIEW_MENU,
} card_view_t;

/** Rows of the decision menu (ux-flows 3.2), in screen order. */
typedef enum {
    CARD_ROW_TERMINAL,
    CARD_ROW_SESSION,
    CARD_ROW_DENY_ASK_WHY,
    CARD_ROW_DENY_STOP,
} card_row_t;

typedef enum {
    /** No grant, high risk, cut, or a server without `decide`: the row is not in the menu. */
    CARD_SESSION_ABSENT,
    /** A grant exists but the plain Allow is not available yet (unread pages, or too many): slate, no hold. */
    CARD_SESSION_LOCKED,
    /** Exactly when the plain Allow is available: the APPROVE commit hold grants. */
    CARD_SESSION_READY,
} card_session_t;

/** The APPROVE tab of the card. */
typedef enum {
    /** Cut or longer than CARD_MAX_PAGES: no allow on the device. */
    CARD_ALLOW_NONE,
    /** Pages not yet shown: slate, a press shakes. */
    CARD_ALLOW_INACTIVE,
    CARD_ALLOW_CLICK,
    /** High risk: a commit hold. */
    CARD_ALLOW_HOLD,
} card_allow_t;

typedef enum {
    CARD_DECISION_NONE,
    CARD_DECISION_ALLOW,
    CARD_DECISION_DENY,
    CARD_DECISION_DEFER,
    CARD_DECISION_ALLOW_SESSION,
    CARD_DECISION_DENY_ASK_WHY,
    CARD_DECISION_DENY_STOP,
} card_decision_t;

typedef enum {
    CARD_OUT_NONE,
    CARD_OUT_PAGE,
    CARD_OUT_MENU_OPEN,
    CARD_OUT_MENU_BACK,
    CARD_OUT_CURSOR,
    /** An inactive tab was pressed. */
    CARD_OUT_SHAKE,
    /** A hold-only tab was clicked (or its hold released early). */
    CARD_OUT_NUDGE,
    CARD_OUT_PROGRESS,
    /** A hold ended without a decision (the card's time ran out). */
    CARD_OUT_CANCEL,
    CARD_OUT_DECIDE,
} card_out_kind_t;

typedef struct {
    card_out_kind_t kind;
    btn_id_t btn;
    card_decision_t decision;
    /** PROGRESS: fill 0..1000. */
    uint16_t permille;
    /** Non-zero: arm the buttons for this long (a new view appeared). */
    uint16_t arm_ms;
} card_out_t;

typedef struct {
    proto_risk_t risk;
    bool cut;
    bool grant;
    /** The server listed `decide`: the menu has its deny rows and the session row may exist. */
    bool decide;
    uint32_t deadline_ms;
    /**
     * Columns the command is wrapped to: CARD_COLS, or two fewer for a shell
     * command, which the well shows behind a "$ " prompt with a hanging indent.
     */
    uint8_t cols;
    /** Pages of the command (capped at 255) and how many must be shown before a yes. */
    uint8_t pages;
    uint8_t required;
    card_view_t view;
    /** Page on screen, 0-based, and the number of pages shown so far (pages are visited in order). */
    uint8_t page;
    uint8_t seen;
    uint8_t cursor;
    bool sent;
    /** A commit hold began on this card and is under way. */
    bool holding;
} card_fsm_t;

/**
 * Opens the card for e on its first page. decide: the server listed the cap.
 * Edit/Write tools (a `cmd` of protocol 14.2) need only the path line read; everything
 * else needs every page.
 */
void card_fsm_open(card_fsm_t *c, const reqq_entry_t *e, bool decide);

/** Every page that must be read was shown, and the command is whole and short enough. */
bool card_fsm_reading_done(const card_fsm_t *c);

/** armed and card_fsm_reading_done(). */
bool card_fsm_allow_enabled(const card_fsm_t *c, bool armed);

card_allow_t card_fsm_allow(const card_fsm_t *c);
card_session_t card_fsm_session(const card_fsm_t *c);

/** The menu rows in order; returns their number (1 without `decide`). */
uint8_t card_fsm_rows(const card_fsm_t *c, card_row_t rows[CARD_MENU_ROWS]);
card_row_t card_fsm_focused_row(const card_fsm_t *c);

/** The mode the app asks btn_fsm for when this button goes down. */
btn_mode_t card_fsm_mode(const card_fsm_t *c, btn_id_t btn, uint32_t now_ms);

/** Applies one button output (swallowed ones do nothing). */
card_out_t card_fsm_button(card_fsm_t *c, const btn_fsm_out_t *out, uint32_t now_ms);

/** The card's time ran out: a hold under way ends (CANCEL) and the card decides nothing any more. */
card_out_t card_fsm_tick(card_fsm_t *c, uint32_t now_ms);

/**
 * The check every sender makes before a decision goes on the wire: false for
 * a sent or expired card, a plain allow before the reading is done, and a
 * session grant without a grant, for a cut or high-risk request, or before
 * every page was shown.
 */
bool card_fsm_may_send(const card_fsm_t *c, card_decision_t d, uint32_t now_ms);

/** The decision went out: the card shows its sent state; false brings the buttons back (error from the server). */
void card_fsm_set_sent(card_fsm_t *c, bool sent);

/** The app moved the screen away from under a press: a hold under way no longer counts. */
void card_fsm_cancel_hold(card_fsm_t *c);

#define CARD_RECEIPT_TITLE_SIZE   48
#define CARD_RECEIPT_DETAIL_SIZE  (PROTO_RULE_SIZE + 32)

typedef enum {
    CARD_TONE_NEUTRAL,
    CARD_TONE_ALLOWED,
    CARD_TONE_DENIED,
} card_tone_t;

/** What the device says after a request left its queue (ux-flows 3.1, 3.2): a title and an optional detail. */
typedef struct {
    card_tone_t tone;
    char title[CARD_RECEIPT_TITLE_SIZE];
    char detail[CARD_RECEIPT_DETAIL_SIZE];
} card_receipt_t;

/**
 * The receipt for a dismiss of e. ours: this device answered it (its answer
 * is waiting for this dismiss, or `by` names this device).
 */
void card_receipt_dismiss(card_receipt_t *r, const proto_dismiss_t *d, bool ours, const reqq_entry_t *e);

/** The receipt when the card's own time ran out. */
void card_receipt_timeout(card_receipt_t *r);

const char *card_decision_name(card_decision_t d);
const char *card_session_name(card_session_t s);
const char *card_row_name(card_row_t r);
