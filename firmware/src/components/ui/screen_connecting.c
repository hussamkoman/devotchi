#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

static lv_obj_t *s_host;
static lv_obj_t *s_state;
static lv_obj_t *s_error;
static lv_obj_t *s_retry;

lv_obj_t *screen_connecting_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *title = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_ACCENT, UI_TOP, true);
    lv_label_set_text_static(title, "Server");
    s_host = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 64, true);
    s_state = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT, 92, true);
    s_error = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_WARN, 124, false);
    s_retry = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 196, true);
    return scr;
}

void ui_connecting_set(const char *host, const char *state, const char *error, const char *retry)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_host, host);
    ui_label_set(s_state, state);
    ui_label_set(s_error, error);
    ui_label_set(s_retry, retry);
    ui_unlock();
}
