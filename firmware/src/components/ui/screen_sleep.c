#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>

/* Layout of the mockup (91). */
#define PET_CX      160
#define PET_CY      78
#define PET_SCALE   10
#define TITLE_Y     148
#define LINE_Y      178
#define BAR_Y       200

static pet_view_t *s_view;
static lv_obj_t *s_line;
static w_countdown_t *s_bar;

lv_obj_t *screen_sleep_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_view = pet_view_create(scr, PET_CX, PET_CY, PET_SCALE);
    pet_view_follow(s_view);
    pet_view_set_mood(s_view, MOOD_SLEEPING);
    lv_obj_t *title = ui_text_line(scr, UI_FONT_TITLE, UI_C_CREAM, TITLE_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(title, "Going to sleep");
    s_line = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, LINE_Y, LV_TEXT_ALIGN_CENTER);
    s_bar = w_countdown_create(scr, BAR_Y, false);
    const ui_tabs_t tabs = {.any = "Any button: stay awake"};
    ui_screen_set_tabs(UI_SCREEN_SLEEP, &tabs);
    return scr;
}

void screen_sleep_set_shown(bool shown)
{
    pet_view_set_visible(s_view, shown);
}

static void apply(uint8_t seconds, uint16_t permille)
{
    char line[24];
    snprintf(line, sizeof(line), seconds == 1 ? "in %u second" : "in %u seconds", seconds);
    ui_label_set(s_line, line);
    w_countdown_set(s_bar, permille, -1, UI_C_FOG);
}

void ui_sleep_set(uint8_t seconds, uint16_t permille)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active_locked()) {
        apply(seconds, permille);
    }
    ui_unlock();
}

void screen_sleep_demo(void)
{
    apply(6, 400);
}
