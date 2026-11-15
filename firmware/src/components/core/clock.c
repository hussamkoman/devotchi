#include "clock.h"

#include <stdio.h>

#define MS_PER_DAY  86400000LL
#define MS_PER_MIN  60000LL

static const char *const WDAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

bool clock_valid(int64_t epoch_ms)
{
    return epoch_ms >= CLOCK_VALID_AFTER_MS;
}

bool clock_offset_ok(int32_t offset_min)
{
    return offset_min >= CLOCK_OFFSET_MIN_MIN && offset_min <= CLOCK_OFFSET_MAX_MIN;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    const int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

/* Days since 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's civil_from_days). */
static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = floor_div(z, 146097);
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int64_t)yoe + era * 400 + (*m <= 2 ? 1 : 0);
}

void clock_local(int64_t epoch_ms, int32_t offset_min, clock_tm_t *out)
{
    const int64_t local = epoch_ms + (int64_t)offset_min * MS_PER_MIN;
    const int64_t days = floor_div(local, MS_PER_DAY);
    const int64_t ms_of_day = local - days * MS_PER_DAY;
    int64_t y;
    unsigned m;
    unsigned d;
    civil_from_days(days, &y, &m, &d);
    out->year = (uint16_t)y;
    out->month = (uint8_t)(m - 1);
    out->mday = (uint8_t)d;
    /* 1970-01-01 was a Thursday. */
    out->wday = (uint8_t)(((days % 7) + 11) % 7);
    out->hour = (uint8_t)(ms_of_day / 3600000);
    out->minute = (uint8_t)((ms_of_day / MS_PER_MIN) % 60);
    out->second = (uint8_t)((ms_of_day / 1000) % 60);
}

void clock_format_time(const clock_tm_t *t, bool h24, char out[CLOCK_TIME_SIZE])
{
    unsigned h = t->hour % 24;
    if (!h24) {
        h = h % 12 == 0 ? 12 : h % 12;
        snprintf(out, CLOCK_TIME_SIZE, "%u:%02u", h, t->minute % 60u);
        return;
    }
    snprintf(out, CLOCK_TIME_SIZE, "%02u:%02u", h, t->minute % 60u);
}

void clock_format_date(const clock_tm_t *t, char out[CLOCK_DATE_SIZE])
{
    snprintf(out, CLOCK_DATE_SIZE, "%s %u %s", WDAYS[t->wday % 7], t->mday % 32u, MONTHS[t->month % 12]);
}

bool clock_is_night(const clock_tm_t *t)
{
    return t->hour >= CLOCK_NIGHT_START_H || t->hour < CLOCK_NIGHT_END_H;
}

uint32_t clock_ms_to_minute(int64_t epoch_ms)
{
    const int64_t into = epoch_ms - floor_div(epoch_ms, MS_PER_MIN) * MS_PER_MIN;
    return (uint32_t)(MS_PER_MIN - into);
}
