#include "ui_priv.h"

#include "settings_menu.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_theme.h"

#include <string.h>

/* Layout of the mockup (50): rows of 29 + 2 px between the status bar and the tabs. */
#define LIST_Y      24
#define LIST_H      186
#define ROW_H       29
#define CONFIRM_TEXT "Press ACTION again to confirm"

_Static_assert(UI_SETTINGS_ROWS == SETTINGS_ROW_COUNT, "one list row per settings row");

static const char *const ICONS[SETTINGS_ROW_COUNT] = {
    [SETTINGS_ROW_BRIGHTNESS] = UI_ICON_SUN,      [SETTINGS_ROW_SCREEN_TIMEOUT] = UI_ICON_CLOCK,
    [SETTINGS_ROW_DEEP_SLEEP] = UI_ICON_MOON,     [SETTINGS_ROW_HAPTICS] = UI_ICON_BUZZ,
    [SETTINGS_ROW_LED] = UI_ICON_BELL,            [SETTINGS_ROW_MODE] = UI_ICON_DESK,
    [SETTINGS_ROW_WIFI_SETUP] = UI_ICON_WIFI,     [SETTINGS_ROW_UNPAIR] = UI_ICON_PLUG,
    [SETTINGS_ROW_FACTORY_RESET] = UI_ICON_RESET, [SETTINGS_ROW_ABOUT] = UI_ICON_INFO,
};

static w_list_t *s_list;

lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_list = w_list_create(scr, LIST_Y, LIST_H, ROW_H);
    ui_screen_set_title(UI_SCREEN_SETTINGS, "Settings");
    return scr;
}

static void apply(const ui_settings_t *s)
{
    w_list_row_t rows[UI_SETTINGS_ROWS];
    const uint8_t cursor = s->cursor < UI_SETTINGS_ROWS ? s->cursor : 0;
    for (int i = 0; i < UI_SETTINGS_ROWS; i++) {
        rows[i] = (w_list_row_t){
            .icon = ICONS[i],
            .label = s->labels[i],
            .value = s->values[i],
            .desc = s->confirming && i == cursor ? CONFIRM_TEXT : NULL,
        };
    }
    w_list_set(s_list, rows, UI_SETTINGS_ROWS, cursor, s->confirming ? W_LIST_FOCUS_CORAL : W_LIST_FOCUS_CREAM);

    /* The buttons of the flat list: REJECT and APPROVE move the cursor, ACTION acts, ACTION hold leaves. */
    const ui_tabs_t tabs = {.tab = {
                                [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Up"},
                                [UI_TAB_ACTION] = {.state = UI_TAB_CLICK_AND_HOLD,
                                                   .label = s->confirming ? "Confirm" : s->action,
                                                   .hold = "Back"},
                                [UI_TAB_APPROVE] = {.state = UI_TAB_CLICK, .label = "Down"},
                            }};
    ui_screen_set_tabs(UI_SCREEN_SETTINGS, &tabs);
}

void ui_settings_set(const ui_settings_t *s)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(s);
    }
    ui_unlock();
}

void screen_settings_demo(void)
{
    static const char *const LABELS[UI_SETTINGS_ROWS] = {
        "Brightness", "Screen timeout", "Deep sleep", "Haptics", "LED", "Mode", "WiFi setup", "Unpair",
        "Factory reset", "About",
    };
    static const char *const VALUES[UI_SETTINGS_ROWS] = {"75 %", "30 s", "20 min", "Gentle", "On", "Ask"};
    ui_settings_t s = {.cursor = 1, .action = "Change"};
    for (int i = 0; i < UI_SETTINGS_ROWS; i++) {
        s.labels[i] = LABELS[i];
        strlcpy(s.values[i], i < 6 ? VALUES[i] : "", sizeof(s.values[i]));
    }
    apply(&s);
}
