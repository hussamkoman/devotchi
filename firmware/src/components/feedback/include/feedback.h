#pragma once

#include "esp_err.h"
#include "fb_seq.h"

#include <stdbool.h>
#include <stdint.h>

/** Marks the report sent when a pattern ends or is stopped. */
#define FEEDBACK_STEP_END  0xFF

typedef struct {
    fb_seq_id_t pattern;
    /** Step index, or FEEDBACK_STEP_END. */
    uint8_t step;
    uint8_t led_pct;
    uint8_t motor_pct;
    uint32_t now_ms;
} feedback_step_t;

/** Runs in the esp_timer task or the caller's task; it must only copy the data and post it. */
typedef void (*feedback_step_cb_t)(const feedback_step_t *step);

/** Configures the LED and motor PWM and reads the haptic and LED settings. cb may be NULL. */
esp_err_t feedback_init(feedback_step_cb_t cb);

/**
 * Plays a pattern to its end (a looping one until feedback_stop). Returns
 * false when a running pattern of higher priority refused it. Never blocks
 * for long; safe from any task.
 */
bool feedback_play(fb_seq_id_t pattern);

/** Plays a pattern for duration_ms (looping patterns repeat until then). */
bool feedback_play_for(fb_seq_id_t pattern, uint32_t duration_ms);

void feedback_stop(void);

/** Re-reads the haptic and LED settings from storage; a running pattern uses them from its next step. */
void feedback_reload_settings(void);

/** Running pattern, or FB_SEQ_COUNT. */
fb_seq_id_t feedback_current(void);
