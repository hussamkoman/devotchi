#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

static lv_obj_t *s_ssid;
static lv_obj_t *s_state;
static lv_obj_t *s_reason;
static lv_obj_t *s_retry;
static lv_obj_t *s_hint;

lv_obj_t *screen_wifi_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *title = ui_label_create(scr, UI_FONT_TITLE, UI_COLOR_ACCENT, UI_TOP, true);
    lv_label_set_text_static(title, "WiFi");
    s_ssid = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT, 62, true);
    s_state = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT_DIM, 90, true);
    s_reason = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_WARN, 120, false);
    s_retry = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 172, true);
    s_hint = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_ACCENT, 216, true);
    return scr;
}

void ui_wifi_set(const char *ssid, const char *state, const char *reason, const char *retry, const char *hint)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_ssid, ssid);
    ui_label_set(s_state, state);
    ui_label_set(s_reason, reason);
    ui_label_set(s_retry, retry);
    ui_label_set(s_hint, hint);
    ui_unlock();
}
