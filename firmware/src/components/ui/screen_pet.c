#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#define STATUS_Y  174

static lv_obj_t *s_project;
static lv_obj_t *s_status;

lv_obj_t *screen_pet_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_project = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, UI_TOP, true);
    lv_obj_set_style_text_align(s_project, LV_TEXT_ALIGN_CENTER, 0);
    pet_view_create(scr);
    s_status = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT, STATUS_Y, true);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    return scr;
}

void ui_pet_set(const char *project, proto_mood_t mood, const char *status)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_project, project);
    pet_view_set_mood(mood);
    ui_label_set(s_status, status);
    ui_unlock();
}
