#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FB_SEQ_IDENTIFY,
    FB_SEQ_REQUEST,
    FB_SEQ_ATTENTION,
    FB_SEQ_NAG,
    FB_SEQ_NEGATIVE,
    FB_SEQ_COMPLETE,
    FB_SEQ_POSITIVE,
    FB_SEQ_TICK,
    FB_SEQ_CLICK,
    /** The breathing LED of request without its buzz, resumed after another pattern while requests wait. */
    FB_SEQ_PENDING,
    /** Short-long buzz: a request card replaced the one just answered (28b). */
    FB_SEQ_NEXT,
    /** The buzz of a button press (Settings > Button clicks); above tick and click so the press's own one-shot cannot cut it. */
    FB_SEQ_PRESS,
    FB_SEQ_COUNT,
} fb_seq_id_t;

typedef enum {
    FB_SEQ_HAPTIC_OFF,
    FB_SEQ_HAPTIC_GENTLE,
    FB_SEQ_HAPTIC_FULL,
} fb_seq_haptic_t;

typedef struct {
    uint8_t led_pct;
    uint8_t motor_pct;
    uint16_t ms;
} fb_seq_step_t;

/** Steps from loop_from to the end repeat when loop is set. */
typedef struct {
    const char *name;
    const fb_seq_step_t *steps;
    uint8_t count;
    uint8_t loop_from;
    bool loop;
    uint8_t priority;
} fb_seq_pattern_t;

typedef struct {
    /** LED duty percent after the LED setting. */
    uint8_t led_pct;
    /** Absolute motor duty percent after the haptic setting and the motor maximum. */
    uint8_t motor_pct;
    uint8_t step;
    /** True on the first poll that returns this step. */
    bool step_started;
    /** Poll again at this time; meaningless when done. */
    uint32_t next_deadline_ms;
    /** Nothing is playing; outputs are 0. */
    bool done;
} fb_seq_out_t;

typedef struct {
    const fb_seq_pattern_t *pattern;
    fb_seq_id_t id;
    uint8_t step;
    bool reported;
    uint32_t start_ms;
    uint32_t step_start_ms;
    /** 0: until the pattern ends (forever for a looping pattern). */
    uint32_t duration_ms;
    fb_seq_haptic_t haptic;
    bool led_enabled;
    /** False mutes the motor of the button patterns (press, tick). */
    bool clicks_enabled;
    uint8_t motor_max_pct;
} fb_seq_t;

const fb_seq_pattern_t *fb_seq_pattern(fb_seq_id_t id);

/** Finds a pattern by name; false for an unknown name. */
bool fb_seq_find(const char *name, fb_seq_id_t *out);

/** Starts idle, with full haptics, LED on and the given motor maximum. */
void fb_seq_init(fb_seq_t *s, uint8_t motor_max_pct);

/** Changes the output scaling; takes effect at the next poll. */
void fb_seq_set_output(fb_seq_t *s, fb_seq_haptic_t haptic, bool led_enabled);
/** Turns the button buzz (press and tick patterns) on or off. */
void fb_seq_set_clicks(fb_seq_t *s, bool enabled);

/**
 * Starts a pattern unless a running one has a higher priority; returns false
 * when it was refused. duration_ms 0 plays the pattern to its end.
 */
bool fb_seq_start(fb_seq_t *s, fb_seq_id_t id, uint32_t duration_ms, uint32_t now_ms);

void fb_seq_stop(fb_seq_t *s);

bool fb_seq_running(const fb_seq_t *s);

/** Running pattern, or FB_SEQ_COUNT when idle. */
fb_seq_id_t fb_seq_current(const fb_seq_t *s);

/**
 * Advances to the step that covers now_ms and returns the outputs. Step
 * boundaries are computed from the previous boundary, so a late poll does not
 * shift the rest of the pattern.
 */
void fb_seq_poll(fb_seq_t *s, uint32_t now_ms, fb_seq_out_t *out);
