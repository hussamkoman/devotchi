#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#define NAME_Y  150
#define HINT_Y  182

static lv_obj_t *s_name;

lv_obj_t *screen_identify_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    /* The name comes from the server, so it is body text: title has no glyphs beyond ASCII. */
    s_name = ui_text_line(scr, UI_FONT_BODY, UI_C_CREAM, NAME_Y, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *hint = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, HINT_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(hint, "This is the device you picked");
    const ui_tabs_t tabs = {.any = "Any button: OK"};
    ui_screen_set_tabs(UI_SCREEN_IDENTIFY, &tabs);
    return scr;
}

static void apply(const char *name)
{
    ui_label_set(s_name, name != NULL && name[0] != '\0' ? name : "Devotchi");
}

void ui_identify_set(const char *name)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active_locked()) {
        apply(name);
    }
    ui_unlock();
}

void screen_identify_demo(void)
{
    apply("devotchi-3F2A");
}
