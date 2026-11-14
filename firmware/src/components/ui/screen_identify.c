#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

static lv_obj_t *s_name;

lv_obj_t *screen_identify_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_name = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_TEXT, 0, false);
    lv_obj_set_style_text_align(s_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_name, LV_ALIGN_LEFT_MID, UI_PAD, 0);
    return scr;
}

void ui_identify_set(const char *name)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_name, name != NULL && name[0] != '\0' ? name : "Devotchi");
    ui_unlock();
}
