#include "app_priv.h"

#include "clock.h"
#include "console.h"
#include "settings_menu.h"
#include "storage.h"

#include "esp_attr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* The offset survives deep sleep with the system time (protocol 14.9); a power-on clears both. */
RTC_DATA_ATTR static int16_t s_rtc_offset_min;
RTC_DATA_ATTR static bool s_rtc_has_offset;

/* App task only, except the console reading the cached settings for `state`. */
static uint8_t s_mode;
static uint8_t s_bri;
static bool s_h24;
static bool s_night_on;
static app_clock_show_t s_show;

void app_clock_reload_settings(void)
{
    s_mode = (uint8_t)storage_get_setting(STORAGE_SETTING_CLOCK);
    s_bri = (uint8_t)storage_get_setting(STORAGE_SETTING_CLOCK_BRI);
    s_h24 = storage_get_setting(STORAGE_SETTING_H24) != 0;
    s_night_on = storage_get_setting(STORAGE_SETTING_NIGHT) != 0;
}

void app_clock_init(void)
{
    app_clock_reload_settings();
}

static int64_t system_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void app_clock_on_time(int64_t epoch_ms, bool has_offset, int16_t offset_min)
{
    if (!clock_valid(epoch_ms)) {
        return;
    }
    /* Display only: the device has no trusted clock (protocol 3.3); deadlines use the monotonic timer. */
    const struct timeval tv = {
        .tv_sec = (time_t)(epoch_ms / 1000),
        .tv_usec = (suseconds_t)(epoch_ms % 1000) * 1000,
    };
    settimeofday(&tv, NULL);
    if (has_offset && clock_offset_ok(offset_min)) {
        s_rtc_offset_min = offset_min;
        s_rtc_has_offset = true;
    }
    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"time\",\"epoch\":%lld,\"offset\":%d,\"valid\":%s}",
             (long long)(epoch_ms / 1000), s_rtc_offset_min, app_clock_valid() ? "true" : "false");
    console_evt(evt);
}

bool app_clock_valid(void)
{
    return s_rtc_has_offset && clock_valid(system_ms());
}

int64_t app_clock_epoch_ms(void)
{
    return app_clock_valid() ? system_ms() : 0;
}

int16_t app_clock_offset_min(void)
{
    return s_rtc_has_offset ? s_rtc_offset_min : 0;
}

bool app_clock_format(int64_t epoch_ms, char time[CLOCK_TIME_SIZE], char date[CLOCK_DATE_SIZE])
{
    time[0] = '\0';
    date[0] = '\0';
    if (!s_rtc_has_offset || !clock_valid(epoch_ms)) {
        return false;
    }
    clock_tm_t t;
    clock_local(epoch_ms, s_rtc_offset_min, &t);
    clock_format_time(&t, s_h24, time);
    clock_format_date(&t, date);
    return true;
}

bool app_clock_night(void)
{
    if (s_show != APP_CLOCK_SHOW_AUTO) {
        return s_show == APP_CLOCK_SHOW_NIGHT;
    }
    if (!s_night_on || !app_clock_valid()) {
        return false;
    }
    clock_tm_t t;
    clock_local(system_ms(), s_rtc_offset_min, &t);
    return clock_is_night(&t);
}

uint8_t app_clock_mode(void)
{
    return s_mode;
}

uint8_t app_clock_brightness(void)
{
    return s_bri;
}

void app_clock_set_show(app_clock_show_t show)
{
    s_show = show;
}

app_clock_show_t app_clock_show(void)
{
    return s_show;
}

/* `clock set <epoch_s> <offset_min>` and `clock show day|night|off` (app task). */
void app_clock_on_request(app_request_t *req)
{
    if (strncmp(req->text, "set", 3) == 0) {
        char *end = NULL;
        const long long epoch_s = strtoll(req->arg, &end, 10);
        const long offset = end != NULL ? strtol(end, NULL, 10) : 0;
        if (!clock_valid(epoch_s * 1000) || !clock_offset_ok((int32_t)offset)) {
            app_request_complete(false, "epoch after 2026-01-01 and offset -720..840");
            return;
        }
        app_clock_on_time(epoch_s * 1000, true, (int16_t)offset);
    } else if (strcmp(req->arg, "day") == 0 || strcmp(req->arg, "night") == 0 || strcmp(req->arg, "off") == 0) {
        s_show = req->arg[0] == 'd' ? APP_CLOCK_SHOW_DAY : req->arg[0] == 'n' ? APP_CLOCK_SHOW_NIGHT
                                                                              : APP_CLOCK_SHOW_AUTO;
    } else {
        app_request_complete(false, "clock set <epoch_s> <offset_min> | clock show day|night|off");
        return;
    }
    app_link_render(app_now_ms());
    app_request_complete(true, "");
}
