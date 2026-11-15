#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Text block of the mockups (04, 05); the space above is the start-up pet's. */
#define STATE_Y      138
#define HOST_Y       168
#define DETAIL_Y     190
#define MONO_COLS    24
#define UNREACHABLE  "Server unreachable"

static lv_obj_t *s_state;
static lv_obj_t *s_host;
static lv_obj_t *s_retry;
static lv_obj_t *s_detail;

/* Guarded by the ui lock. */
static bool s_valid;
static bool s_offline;

lv_obj_t *screen_connecting_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_state = ui_text_line(scr, UI_FONT_TITLE, UI_C_CREAM, STATE_Y, LV_TEXT_ALIGN_CENTER);
    /* The host in mono and, while offline, the retry countdown after it, centred together. */
    lv_obj_t *row = ui_box(scr, UI_TEXT_X, HOST_Y, UI_WIDTH_TEXT, lv_font_get_line_height(UI_FONT_MONO));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_host = ui_text(row, UI_FONT_MONO, UI_C_SKY, 0, 0, 0, true);
    s_retry = ui_text(row, UI_FONT_SMALL, UI_C_FOG, 0, 0, 0, true);
    s_detail = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, DETAIL_Y, LV_TEXT_ALIGN_CENTER);
    ui_screen_set_title(UI_SCREEN_CONNECTING, "Server");
    const ui_tabs_t tabs = {.tab = {[UI_TAB_ACTION] = {.state = UI_TAB_HOLD_ONLY, .hold = "Settings"}}};
    ui_screen_set_tabs(UI_SCREEN_CONNECTING, &tabs);
    return scr;
}

static void apply(const char *host, const char *state, const char *error, const char *retry)
{
    char name[UI_TEXT_SIZE];
    char after[UI_TEXT_SIZE] = "";
    /* The title already says it when the error is just that the server is unreachable. */
    const bool has_error = error != NULL && error[0] != '\0' && strcmp(error, UNREACHABLE) != 0;
    const bool has_retry = retry != NULL && retry[0] != '\0';
    const bool offline = has_error || has_retry;
    ui_host_text(name, sizeof(name), host, MONO_COLS);
    if (has_retry) {
        snprintf(after, sizeof(after), " \xC2\xB7 %s", retry);
    }
    if (!s_valid || offline != s_offline) {
        lv_obj_set_style_text_color(s_host, offline ? UI_C_FOG : UI_C_SKY, 0);
        s_offline = offline;
        s_valid = true;
    }
    ui_label_set(s_state, offline ? UNREACHABLE : "Finding your server");
    ui_label_set(s_host, name);
    ui_label_set(s_retry, after);
    /* Offline (05): what went wrong, else what happens to requests meanwhile. */
    ui_label_set(s_detail, !offline ? state : has_error ? error : "Requests go to your terminal meanwhile");
}

void ui_connecting_set(const char *host, const char *state, const char *error, const char *retry)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active_locked()) {
        apply(host, state, error, retry);
    }
    ui_unlock();
}

void screen_connecting_demo(bool offline)
{
    apply("ws://devotchi.local:8080", "Connecting\xE2\x80\xA6", "", offline ? "retry in 8 s" : "");
}
