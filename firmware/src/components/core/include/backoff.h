#pragma once

#include <stdint.h>

/** Delays vary by up to this many permille either way. */
#define BACKOFF_JITTER_PERMILLE  200

/** Returns a uniformly distributed 32-bit value. */
typedef uint32_t (*backoff_rand_fn)(void *ctx);

typedef struct {
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t next_ms;
    backoff_rand_fn rand;
    void *rand_ctx;
} backoff_t;

/** rand may be NULL for delays without jitter. */
void backoff_init(backoff_t *b, uint32_t min_ms, uint32_t max_ms, backoff_rand_fn rand, void *rand_ctx);

/** Returns the next delay: min, 2 x min, 4 x min ... capped at max, each with jitter. */
uint32_t backoff_next(backoff_t *b);

/** Starts the next sequence again at min (after a success). */
void backoff_reset(backoff_t *b);
