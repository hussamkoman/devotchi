#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

static lv_obj_t *s_service;
static lv_obj_t *s_pop;
static lv_obj_t *s_status;

lv_obj_t *screen_provisioning_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *title = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_ACCENT, UI_PAD, true);
    lv_label_set_text_static(title, "WiFi setup");
    lv_obj_t *how = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 48, true);
    lv_label_set_text_static(how, "Use the ESP BLE Provisioning app");

    lv_obj_t *service_caption = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 78, true);
    lv_label_set_text_static(service_caption, "Device");
    s_service = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT, 96, true);

    lv_obj_t *pop_caption = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 128, true);
    lv_label_set_text_static(pop_caption, "Proof of possession");
    s_pop = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_TEXT, 146, true);
    lv_obj_add_flag(s_pop, UI_FLAG_SECRET);

    s_status = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_WARN, 196, false);
    return scr;
}

void ui_provisioning_set(const char *service_name, const char *pop, const char *status)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_service, service_name);
    ui_label_set(s_pop, pop);
    ui_label_set(s_status, status);
    ui_unlock();
}
