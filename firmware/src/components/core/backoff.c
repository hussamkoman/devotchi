#include "backoff.h"

#include <stddef.h>

void backoff_init(backoff_t *b, uint32_t min_ms, uint32_t max_ms, backoff_rand_fn rand, void *rand_ctx)
{
    if (min_ms == 0) {
        min_ms = 1;
    }
    *b = (backoff_t){
        .min_ms = min_ms,
        .max_ms = max_ms < min_ms ? min_ms : max_ms,
        .next_ms = min_ms,
        .rand = rand,
        .rand_ctx = rand_ctx,
    };
}

uint32_t backoff_next(backoff_t *b)
{
    const uint32_t base = b->next_ms;
    b->next_ms = base > b->max_ms / 2 ? b->max_ms : base * 2;

    if (b->rand == NULL) {
        return base;
    }
    const uint32_t span = 2 * BACKOFF_JITTER_PERMILLE + 1;
    const int64_t permille = (int64_t)(b->rand(b->rand_ctx) % span) - BACKOFF_JITTER_PERMILLE;
    const int64_t delay = (int64_t)base + (int64_t)base * permille / 1000;
    if (delay < 1) {
        return 1;
    }
    return delay > UINT32_MAX ? UINT32_MAX : (uint32_t)delay;
}

void backoff_reset(backoff_t *b)
{
    b->next_ms = b->min_ms;
}
