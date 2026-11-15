#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Epoch milliseconds of 2026-01-01T00:00:00Z: an earlier system time was never set. */
#define CLOCK_VALID_AFTER_MS  1767225600000LL
/** Protocol 14.9: minutes east of UTC. */
#define CLOCK_OFFSET_MIN_MIN  (-720)
#define CLOCK_OFFSET_MAX_MIN  840
/** Night (design-system 2.4) runs from this hour to CLOCK_NIGHT_END_H, local time. */
#define CLOCK_NIGHT_START_H   22
#define CLOCK_NIGHT_END_H     7

#define CLOCK_TIME_SIZE  8
#define CLOCK_DATE_SIZE  16

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    /** 0 = Sunday. */
    uint8_t wday;
    uint8_t mday;
    /** 0 = January. */
    uint8_t month;
    uint16_t year;
} clock_tm_t;

/** True for a system time the server set (after 2026-01-01). */
bool clock_valid(int64_t epoch_ms);

/** True for an offset protocol 14.9 allows. */
bool clock_offset_ok(int32_t offset_min);

/** Local calendar time of epoch_ms shifted by offset_min. */
void clock_local(int64_t epoch_ms, int32_t offset_min, clock_tm_t *out);

/** "14:32" (24 h) or "2:32" (12 h, no leading zero, no AM/PM). */
void clock_format_time(const clock_tm_t *t, bool h24, char out[CLOCK_TIME_SIZE]);

/** "SUN 15 NOV". */
void clock_format_date(const clock_tm_t *t, char out[CLOCK_DATE_SIZE]);

/** 22:00 to 07:00 local time. */
bool clock_is_night(const clock_tm_t *t);

/** Milliseconds from epoch_ms to the next full local minute (1..60000). */
uint32_t clock_ms_to_minute(int64_t epoch_ms);
