#include "fb_seq.h"

#include <stddef.h>
#include <string.h>

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* Motor and LED tracks with different timings are merged at every boundary of either track. */

static const fb_seq_step_t IDENTIFY[] = {
    {100, 80, 100}, {100, 0, 200}, {100, 80, 100}, {100, 0, 200}, {100, 80, 100}, {100, 0, 200},
};

static const fb_seq_step_t REQUEST[] = {
    {100, 100, 120}, {100, 0, 80}, {100, 100, 120},
    {0, 0, 250}, {25, 0, 250}, {50, 0, 250}, {75, 0, 250},
    {100, 0, 250}, {75, 0, 250}, {50, 0, 250}, {25, 0, 250},
};

static const fb_seq_step_t ATTENTION[] = {
    {0, 100, 100}, {0, 0, 100}, {0, 100, 100}, {100, 0, 200}, {0, 0, 1800},
};

static const fb_seq_step_t NAG[] = {
    {0, 100, 60},
};

/* Motor 60 x250, off 150, 60 x250; LED 100 x300, 0 x300, twice. */
static const fb_seq_step_t NEGATIVE[] = {
    {100, 60, 250}, {100, 0, 50}, {0, 0, 100}, {0, 60, 200}, {100, 60, 50}, {100, 0, 250}, {0, 0, 300},
};

/* Motor 50 x60, off 60, 100 x150; LED 100, 75, 50, 25, 0 at 200 ms each. */
static const fb_seq_step_t COMPLETE[] = {
    {100, 50, 60}, {100, 0, 60}, {100, 100, 80}, {75, 100, 70}, {75, 0, 130},
    {50, 0, 200}, {25, 0, 200}, {0, 0, 200},
};

/* LED 3 x [100 x80, 0 x80]; motor 60 x40. */
static const fb_seq_step_t POSITIVE[] = {
    {100, 60, 40}, {100, 0, 40}, {0, 0, 80}, {100, 0, 80}, {0, 0, 80}, {100, 0, 80}, {0, 0, 80},
};

static const fb_seq_step_t TICK[] = {
    {0, 50, 30},
};

static const fb_seq_step_t CLICK[] = {
    {100, 0, 40},
};

/* Motor 60 x35 with the click's LED flash, so the press's own click adds nothing. */
static const fb_seq_step_t PRESS[] = {
    {100, 60, 35},
    {100, 0, 5},
};

static const fb_seq_step_t PENDING[] = {
    {0, 0, 250}, {25, 0, 250}, {50, 0, 250}, {75, 0, 250},
    {100, 0, 250}, {75, 0, 250}, {50, 0, 250}, {25, 0, 250},
};

/* Motor 60 x60, off 80, 100 x180. */
static const fb_seq_step_t NEXT[] = {
    {0, 60, 60}, {0, 0, 80}, {0, 100, 180},
};

static const fb_seq_pattern_t PATTERNS[FB_SEQ_COUNT] = {
    [FB_SEQ_IDENTIFY] = {"identify", IDENTIFY, COUNT_OF(IDENTIFY), 0, true, 3},
    [FB_SEQ_REQUEST] = {"request", REQUEST, COUNT_OF(REQUEST), 3, true, 2},
    [FB_SEQ_ATTENTION] = {"attention", ATTENTION, COUNT_OF(ATTENTION), 3, true, 2},
    [FB_SEQ_NAG] = {"nag", NAG, COUNT_OF(NAG), 0, false, 2},
    [FB_SEQ_NEGATIVE] = {"negative", NEGATIVE, COUNT_OF(NEGATIVE), 0, false, 1},
    [FB_SEQ_COMPLETE] = {"complete", COMPLETE, COUNT_OF(COMPLETE), 0, false, 1},
    [FB_SEQ_POSITIVE] = {"positive", POSITIVE, COUNT_OF(POSITIVE), 0, false, 1},
    [FB_SEQ_TICK] = {"tick", TICK, COUNT_OF(TICK), 0, false, 0},
    [FB_SEQ_CLICK] = {"click", CLICK, COUNT_OF(CLICK), 0, false, 0},
    [FB_SEQ_PRESS] = {"press", PRESS, COUNT_OF(PRESS), 0, false, 1},
    [FB_SEQ_PENDING] = {"pending", PENDING, COUNT_OF(PENDING), 0, true, 0},
    [FB_SEQ_NEXT] = {"next", NEXT, COUNT_OF(NEXT), 0, false, 1},
};

const fb_seq_pattern_t *fb_seq_pattern(fb_seq_id_t id)
{
    return id < FB_SEQ_COUNT ? &PATTERNS[id] : NULL;
}

bool fb_seq_find(const char *name, fb_seq_id_t *out)
{
    for (size_t i = 0; i < FB_SEQ_COUNT; i++) {
        if (strcmp(name, PATTERNS[i].name) == 0) {
            *out = (fb_seq_id_t)i;
            return true;
        }
    }
    return false;
}

void fb_seq_init(fb_seq_t *s, uint8_t motor_max_pct)
{
    *s = (fb_seq_t){
        .id = FB_SEQ_COUNT,
        .haptic = FB_SEQ_HAPTIC_FULL,
        .led_enabled = true,
        .clicks_enabled = true,
        .motor_max_pct = motor_max_pct > 100 ? 100 : motor_max_pct,
    };
}

void fb_seq_set_output(fb_seq_t *s, fb_seq_haptic_t haptic, bool led_enabled)
{
    s->haptic = haptic;
    s->led_enabled = led_enabled;
}

void fb_seq_set_clicks(fb_seq_t *s, bool enabled)
{
    s->clicks_enabled = enabled;
}

bool fb_seq_running(const fb_seq_t *s)
{
    return s->pattern != NULL;
}

fb_seq_id_t fb_seq_current(const fb_seq_t *s)
{
    return s->pattern != NULL ? s->id : FB_SEQ_COUNT;
}

bool fb_seq_start(fb_seq_t *s, fb_seq_id_t id, uint32_t duration_ms, uint32_t now_ms)
{
    const fb_seq_pattern_t *p = fb_seq_pattern(id);
    if (p == NULL) {
        return false;
    }
    if (s->pattern != NULL && p->priority < s->pattern->priority) {
        return false;
    }
    s->pattern = p;
    s->id = id;
    s->step = 0;
    s->reported = false;
    s->start_ms = now_ms;
    s->step_start_ms = now_ms;
    s->duration_ms = duration_ms;
    return true;
}

void fb_seq_stop(fb_seq_t *s)
{
    s->pattern = NULL;
    s->id = FB_SEQ_COUNT;
}

static uint8_t scale_motor(const fb_seq_t *s, uint8_t pct)
{
    static const uint32_t HAPTIC_PCT[] = {
        [FB_SEQ_HAPTIC_OFF] = 0,
        [FB_SEQ_HAPTIC_GENTLE] = 50,
        [FB_SEQ_HAPTIC_FULL] = 100,
    };
    if (!s->clicks_enabled && (s->id == FB_SEQ_PRESS || s->id == FB_SEQ_TICK)) {
        return 0;
    }
    const uint32_t haptic = s->haptic <= FB_SEQ_HAPTIC_FULL ? HAPTIC_PCT[s->haptic] : 0;
    return (uint8_t)((pct * haptic * s->motor_max_pct + 5000U) / 10000U);
}

static bool expired(const fb_seq_t *s, uint32_t now_ms)
{
    return s->duration_ms != 0 && (uint32_t)(now_ms - s->start_ms) >= s->duration_ms;
}

void fb_seq_poll(fb_seq_t *s, uint32_t now_ms, fb_seq_out_t *out)
{
    *out = (fb_seq_out_t){.done = true};
    const fb_seq_pattern_t *p = s->pattern;
    if (p == NULL) {
        return;
    }
    if (expired(s, now_ms)) {
        fb_seq_stop(s);
        return;
    }
    while ((uint32_t)(now_ms - s->step_start_ms) >= p->steps[s->step].ms) {
        s->step_start_ms += p->steps[s->step].ms;
        s->step++;
        s->reported = false;
        if (s->step >= p->count) {
            if (!p->loop) {
                fb_seq_stop(s);
                return;
            }
            s->step = p->loop_from;
        }
    }

    const fb_seq_step_t *step = &p->steps[s->step];
    uint32_t deadline = s->step_start_ms + step->ms;
    if (s->duration_ms != 0) {
        const uint32_t to_end = s->duration_ms - (now_ms - s->start_ms);
        if (to_end < deadline - now_ms) {
            deadline = now_ms + to_end;
        }
    }
    *out = (fb_seq_out_t){
        .led_pct = s->led_enabled ? step->led_pct : 0,
        .motor_pct = scale_motor(s, step->motor_pct),
        .step = s->step,
        .step_started = !s->reported,
        .next_deadline_ms = deadline,
        .done = false,
    };
    s->reported = true;
}
