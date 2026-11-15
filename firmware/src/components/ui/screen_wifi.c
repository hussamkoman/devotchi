#include "ui_priv.h"

#include "text.h"
#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Text block of the mockup (03); the space above is the start-up pet's. */
#define STATE_Y   138
#define SSID_Y    168
#define DETAIL_Y  190
#define MONO_COLS 34

static lv_obj_t *s_state;
static lv_obj_t *s_ssid;
static lv_obj_t *s_detail;

/* Guarded by the ui lock. */
static bool s_valid;
static bool s_can_setup;

lv_obj_t *screen_wifi_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_state = ui_text_line(scr, UI_FONT_TITLE, UI_C_CREAM, STATE_Y, LV_TEXT_ALIGN_CENTER);
    s_ssid = ui_text_line(scr, UI_FONT_MONO, UI_C_SKY, SSID_Y, LV_TEXT_ALIGN_CENTER);
    s_detail = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, DETAIL_Y, LV_TEXT_ALIGN_CENTER);
    ui_screen_set_title(UI_SCREEN_WIFI, "Wi-Fi");
    s_valid = false;
    return scr;
}

static void apply(const char *ssid, const char *state, const char *reason, const char *retry, bool can_setup)
{
    char line[UI_TEXT_SIZE];
    char name[UI_TEXT_SIZE];
    const bool has_reason = reason != NULL && reason[0] != '\0';
    const bool has_retry = retry != NULL && retry[0] != '\0';
    snprintf(line, sizeof(line), "%s%s%s", has_reason ? reason : "", has_reason && has_retry ? " \xC2\xB7 " : "",
             has_retry ? retry : "");
    text_truncate_middle(name, sizeof(name), ssid != NULL ? ssid : "", MONO_COLS);
    ui_label_set(s_state, state);
    ui_label_set(s_ssid, name);
    ui_label_set(s_detail, line);
    if (!s_valid || can_setup != s_can_setup) {
        ui_tabs_t tabs = {.tab = {[UI_TAB_ACTION] = {.state = UI_TAB_HOLD_ONLY, .hold = "Settings"}}};
        if (can_setup) {
            tabs.tab[UI_TAB_APPROVE] = (ui_tab_t){.state = UI_TAB_CLICK, .label = "Set up"};
        }
        ui_screen_set_tabs(UI_SCREEN_WIFI, &tabs);
        s_can_setup = can_setup;
        s_valid = true;
    }
}

void ui_wifi_set(const char *ssid, const char *state, const char *reason, const char *retry, bool can_setup)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(ssid, state, reason, retry, can_setup);
    }
    ui_unlock();
}

void screen_wifi_demo(void)
{
    apply("HomeNet-5G", "Joining Wi-Fi", "Attempt 3", "wrong password?", true);
}
