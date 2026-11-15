#pragma once

#include "btn_fsm.h"
#include "reqq.h"

#include <stdbool.h>
#include <stddef.h>
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
/** A question shows this many lines of its text above the options; a longer one opens on a read page. */
#define CARD_Q_LINES     2
/** Lines of a question's text per read page (ux-flows 4). */
#define CARD_READ_LINES  6
/** Plan steps per page, and the pages a plan may have to be approved on the device (ux-flows 3.3). */
#define CARD_PLAN_STEPS      4
#define CARD_PLAN_MAX_PAGES  3
/** `extend` (protocol 14.6): at most once per this long per request, and only with less than this left. */
#define CARD_EXTEND_GAP_MS    5000
#define CARD_EXTEND_BELOW_MS  55000

typedef enum {
    CARD_VIEW_CARD,
    CARD_VIEW_MENU,
    /** A question's whole text, CARD_READ_LINES lines per page (33). */
    CARD_VIEW_READ,
    /** Every answer of a call with several questions, before they are sent (37). */
    CARD_VIEW_REVIEW,
} card_view_t;

/** Rows of the decision menu (ux-flows 3.2), the question menu (36) and the plan menu (29b). */
typedef enum {
    CARD_ROW_TERMINAL,
    CARD_ROW_SESSION,
    CARD_ROW_DENY_ASK_WHY,
    CARD_ROW_DENY_STOP,
    CARD_ROW_READ_AGAIN,
    CARD_ROW_LET_CLAUDE,
    CARD_ROW_DECLINE,
    CARD_ROW_ACCEPT_EDITS,
} card_row_t;

/** The session row of the decision menu; for a plan, the "Approve, accept all edits" row. */
typedef enum {
    /** No grant, high risk, cut, or a server without `decide`: the row is not in the menu. */
    CARD_SESSION_ABSENT,
    /** A grant exists but the plain Allow is not available yet (unread pages, or too many): slate, no hold. */
    CARD_SESSION_LOCKED,
    /** Exactly when the plain Allow is available: the APPROVE commit hold grants. */
    CARD_SESSION_READY,
} card_session_t;

/** The APPROVE tab of a permission card; for a plan, its Approve tab. */
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
    /** question_response with every question answered. */
    CARD_DECISION_ANSWER,
    /** question_response with `null` for every question not answered yet ("Let Claude decide"). */
    CARD_DECISION_NO_PREFERENCE,
    CARD_DECISION_DECLINE,
    CARD_DECISION_APPROVE,
    CARD_DECISION_APPROVE_EDITS,
    CARD_DECISION_REVISE,
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
    /** Another question, its read page, its options or the review card came on screen. */
    CARD_OUT_VIEW,
    /** A multi-select option was ticked or unticked. */
    CARD_OUT_TICK,
} card_out_kind_t;

typedef struct {
    card_out_kind_t kind;
    btn_id_t btn;
    card_decision_t decision;
    /** PROGRESS: fill 0..1000. */
    uint16_t permille;
    /** Non-zero: arm the buttons for this long (a new view appeared). */
    uint16_t arm_ms;
    /** Send `extend` for the request (a press on a question or plan, within the limits of protocol 14.6). */
    bool extend;
} card_out_t;

typedef struct {
    reqq_kind_t kind;
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

    /* Questions: the call's questions, the one on screen, and the answers so far. */
    uint8_t q_count;
    uint8_t q;
    uint8_t n_options[PROTO_Q_MAX];
    bool multi[PROTO_Q_MAX];
    /** Read pages of each question's text; 0 when it fits above the options. */
    uint8_t read_pages[PROTO_Q_MAX];
    /** Bit per option: the choice, or the ticks of a multi-select. */
    uint8_t mask[PROTO_Q_MAX];
    /** Bit per question that has its answer. */
    uint8_t answered;
    /** Focused option row (the multi-select send row comes after the options). */
    uint8_t opt;
    /** The view the question menu returns to. */
    card_view_t back_view;
    /* Plans. */
    uint8_t n_steps;
    uint16_t more;
    /* `extend`: the server lists the cap, and when the last one went out. */
    bool can_extend;
    bool extended;
    uint32_t extend_ms;
} card_fsm_t;

/**
 * Opens the card for e on its first page. decide: the server listed the cap.
 * Edit/Write tools (a `cmd` of protocol 14.2) need only the path line read; everything
 * else needs every page.
 */
void card_fsm_open(card_fsm_t *c, const reqq_entry_t *e, bool decide);

/**
 * Opens the card of a question or plan entry (e->slot) on its first view.
 * text_lines[i]: lines of `body` text question i takes (measured by the
 * display; ignored for a plan). caps: the server's PROTO_CAP_* list.
 */
void card_fsm_open_ask(card_fsm_t *c, const reqq_entry_t *e, const uint16_t text_lines[PROTO_Q_MAX], uint32_t caps);

/** A `deadline` moved the request's deadline. */
void card_fsm_set_deadline(card_fsm_t *c, uint32_t deadline_ms);

/** Rows of the question on screen: its options, then the send row of a multi-select. */
uint8_t card_fsm_q_rows(const card_fsm_t *c);

/**
 * The `answers` of a question decision (ANSWER or NO_PREFERENCE), one per
 * question in order; returns their number, 0 for any other decision or
 * when ANSWER finds a question without its answer.
 */
size_t card_fsm_answers(const card_fsm_t *c, card_decision_t d, proto_answer_t out[PROTO_Q_MAX]);

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
/* Long enough for a session rule or a line of answers; the receipt shows one line. */
#define CARD_RECEIPT_DETAIL_SIZE  192

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

/**
 * What an answer says, from the question's own labels: the option, the
 * options of a multi-select joined by ", ", "None of these", or "No preference".
 */
void card_answer_text(char *dst, size_t cap, const qstore_question_t *q, const proto_answer_t *a);

const char *card_decision_name(card_decision_t d);
const char *card_session_name(card_session_t s);
const char *card_row_name(card_row_t r);
const char *card_view_name(card_view_t v);
