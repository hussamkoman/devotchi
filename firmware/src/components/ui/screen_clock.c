#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <string.h>

/* Layout of the mockups (40-42): digits, the date in caps, the sleeping pet at 9/16. */
#define TIME_Y      14
#define DATE_Y      92
#define PET_CX      160
#define PET_CY      166
#define PET_SCALE   9
#define PET_FRAME_MS 100

static lv_obj_t *s_scr;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static pet_view_t *s_view;
static bool s_shown;
/* Guarded by the ui lock. */
static ui_clock_t s_clock;
static char s_time_text[8];
static char s_date_text[16];
static bool s_valid;

lv_obj_t *screen_clock_create(void)
{
    s_scr = ui_screen_base_create();
    s_time = ui_text_line(s_scr, UI_FONT_DISPLAY, UI_C_CREAM, TIME_Y, LV_TEXT_ALIGN_CENTER);
    s_date = ui_text_line(s_scr, UI_FONT_SMALL, UI_C_FOG, DATE_Y, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_style(s_date, &ui_style_caps, 0);
    s_view = pet_view_create(s_scr, PET_CX, PET_CY, PET_SCALE);
    pet_view_follow(s_view);
    pet_view_set_mood(s_view, MOOD_SLEEPING);
    pet_view_set_frame_ms(s_view, PET_FRAME_MS);
    return s_scr;
}

void screen_clock_set_shown(bool shown)
{
    s_shown = shown;
    pet_view_set_visible(s_view, shown);
}

/* Labels change once a minute; the night switch restyles everything once. */
static void apply(const ui_clock_t *c)
{
    const bool restyle = !s_valid || c->night != s_clock.night;
    ui_label_set(s_time, c->time != NULL ? c->time : "");
    ui_label_set_caps(s_date, c->date != NULL ? c->date : "");
    strlcpy(s_time_text, c->time != NULL ? c->time : "", sizeof(s_time_text));
    strlcpy(s_date_text, c->date != NULL ? c->date : "", sizeof(s_date_text));
    s_clock = (ui_clock_t){.time = s_time_text, .date = s_date_text, .night = c->night};
    s_valid = true;
    if (restyle) {
        lv_obj_set_style_bg_color(s_scr, c->night ? UI_C_NIGHT_BG : UI_C_INK, 0);
        lv_obj_set_style_text_color(s_time, c->night ? UI_C_EMBER : UI_C_CREAM, 0);
        lv_obj_set_style_text_color(s_date, c->night ? UI_C_EMBER_DIM : UI_C_FOG, 0);
        pet_view_set_outline(s_view, c->night);
    }
}

void ui_clock_set(const ui_clock_t *c)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(c);
    }
    ui_unlock();
}

void ui_clock_get(ui_clock_t *out, char time[8], char date[16])
{
    *out = (ui_clock_t){.time = time, .date = date};
    time[0] = '\0';
    date[0] = '\0';
    if (!ui_lock()) {
        return;
    }
    strlcpy(time, s_time_text, 8);
    strlcpy(date, s_date_text, 16);
    out->night = s_clock.night;
    ui_unlock();
}

void screen_clock_demo(int variant)
{
    static const ui_clock_t DEMOS[] = {
        {.time = "14:32", .date = "Sun 15 Nov", .night = false},
        {.time = "02:07", .date = "Mon 16 Nov", .night = true},
        {.time = "", .date = "", .night = false},
    };
    apply(&DEMOS[variant >= 0 && variant < 3 ? variant : 0]);
}
