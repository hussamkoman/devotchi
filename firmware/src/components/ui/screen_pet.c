#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Status block of the mockups (10-14): the project in caps above one status line. */
#define PROJECT_Y        185
#define STATUS_Y         205
#define STATUS_ALONE_Y   190
#define STATUS_X         16
#define BOOT_TABS_MS     10000

#define ABOUT_Y          28
#define ABOUT_ROW_H      21
#define ABOUT_LABEL_W    86
#define ABOUT_VALUE_W    210

static const char *const ABOUT_LABELS[UI_ABOUT_COUNT] = {
    [UI_ABOUT_NAME] = "Name",     [UI_ABOUT_FW] = "Firmware", [UI_ABOUT_DEVICE_ID] = "Device",
    [UI_ABOUT_SSID] = "Wi-Fi",    [UI_ABOUT_RSSI] = "RSSI",   [UI_ABOUT_IP] = "IP",
    [UI_ABOUT_SERVER] = "Server", [UI_ABOUT_LINK] = "Link",   [UI_ABOUT_UPTIME] = "Uptime",
    [UI_ABOUT_RESET] = "Reset",   [UI_ABOUT_ERROR] = "Last err", [UI_ABOUT_NVS] = "NVS",
};

/* Body centre of the home pet (mockups 10-16) and its scale in sixteenths. */
#define PET_CX           160
#define PET_CY           100
#define PET_SCALE        15
#define BANNER_LIFT_PX   4

static pet_view_t *s_view;
static lv_obj_t *s_project;
static lv_obj_t *s_status;
static lv_obj_t *s_about[2];
static lv_obj_t *s_about_values[UI_ABOUT_COUNT];
static lv_timer_t *s_tabs_timer;

/* Guarded by the ui lock. */
static ui_pet_page_t s_page = UI_PET_PAGE_PET;
static bool s_shown;
static bool s_attention;
static bool s_tabs_on;
static bool s_boot_tabs_done;
static bool s_has_project;
static bool s_error_shown;

static void apply_tabs(void)
{
    static const ui_tabs_t ANY = {.any = "Any button: OK"};
    static const ui_tabs_t HOME = {.tab = {
                                       [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Poke"},
                                       [UI_TAB_ACTION] = {.state = UI_TAB_CLICK_AND_HOLD, .label = "About",
                                                          .hold = "Settings"},
                                       [UI_TAB_APPROVE] = {.state = UI_TAB_CLICK, .label = "Pat"},
                                   }};
    static const ui_tabs_t ABOUT[2] = {
        {.tab = {[UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                 [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Page"}}},
        {.tab = {[UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                 [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Home"}}},
    };
    const ui_tabs_t *tabs = NULL;
    if (s_attention) {
        tabs = &ANY;
    } else if (s_page == UI_PET_PAGE_ABOUT || s_page == UI_PET_PAGE_ABOUT2) {
        tabs = &ABOUT[s_page == UI_PET_PAGE_ABOUT2];
    } else if (s_tabs_on) {
        tabs = &HOME;
    }
    ui_screen_set_tabs(UI_SCREEN_PET, tabs);
}

static void apply_page(void)
{
    static const char *const TITLES[UI_PET_PAGE_COUNT] = {
        [UI_PET_PAGE_PET] = "", [UI_PET_PAGE_ABOUT] = "About 1/2", [UI_PET_PAGE_ABOUT2] = "About 2/2"};
    const bool pet = s_page == UI_PET_PAGE_PET;
    ui_set_hidden(s_about[0], s_page != UI_PET_PAGE_ABOUT);
    ui_set_hidden(s_about[1], s_page != UI_PET_PAGE_ABOUT2);
    ui_set_hidden(s_project, !pet || !s_has_project);
    ui_set_hidden(s_status, !pet);
    ui_screen_set_title(UI_SCREEN_PET, TITLES[s_page]);
    pet_view_set_visible(s_view, s_shown && pet);
    apply_tabs();
}

static void tabs_expired(lv_timer_t *t)
{
    lv_timer_pause(t);
    s_tabs_on = false;
    apply_tabs();
}

static void show_tabs(uint32_t ms)
{
    s_tabs_on = true;
    lv_timer_set_period(s_tabs_timer, ms);
    lv_timer_reset(s_tabs_timer);
    lv_timer_resume(s_tabs_timer);
    apply_tabs();
}

static lv_obj_t *about_page(lv_obj_t *scr, int first, int last)
{
    lv_obj_t *page = ui_box(scr, UI_TEXT_X, ABOUT_Y, UI_WIDTH_TEXT, (last - first) * ABOUT_ROW_H);
    for (int i = first; i < last; i++) {
        const int32_t y = (i - first) * ABOUT_ROW_H;
        lv_obj_t *label = ui_text(page, UI_FONT_SMALL, UI_C_FOG, 0, y + 1, ABOUT_LABEL_W, true);
        lv_label_set_text_static(label, ABOUT_LABELS[i]);
        s_about_values[i] = ui_text(page, UI_FONT_MONO, UI_C_CREAM, ABOUT_LABEL_W, y + 1, ABOUT_VALUE_W, true);
    }
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}

lv_obj_t *screen_pet_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_view = pet_view_create(scr, PET_CX, PET_CY, PET_SCALE);
    pet_view_follow(s_view);
    /* Decoration only: the project name repeats what the agent's status line is about. */
    s_project = ui_text_line(scr, UI_FONT_SMALL, UI_C_SLATE, PROJECT_Y, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_style(s_project, &ui_style_caps, 0);
    lv_obj_set_style_text_color(s_project, UI_C_SLATE, 0);
    s_status = ui_text(scr, UI_FONT_BODY, UI_C_CREAM, STATUS_X, STATUS_ALONE_Y, LV_HOR_RES - 2 * STATUS_X, true);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    s_about[0] = about_page(scr, 0, UI_ABOUT_PAGE1_ROWS);
    s_about[1] = about_page(scr, UI_ABOUT_PAGE1_ROWS, UI_ABOUT_COUNT);
    s_tabs_timer = lv_timer_create(tabs_expired, BOOT_TABS_MS, NULL);
    lv_timer_pause(s_tabs_timer);
    apply_page();
    return scr;
}

void screen_pet_set_shown(bool shown)
{
    s_shown = shown;
    if (shown && !s_boot_tabs_done) {
        s_boot_tabs_done = true;
        show_tabs(BOOT_TABS_MS);
    }
    apply_page();
}

void screen_pet_set_attention(bool on)
{
    if (on != s_attention) {
        s_attention = on;
        apply_tabs();
    }
}

static void apply(const char *project, proto_mood_t mood, const char *status)
{
    const bool has_project = project != NULL && project[0] != '\0';
    ui_label_set_caps(s_project, project);
    if (has_project != s_has_project) {
        s_has_project = has_project;
        lv_obj_set_y(s_status, has_project ? STATUS_Y : STATUS_ALONE_Y);
        ui_set_hidden(s_project, s_page != UI_PET_PAGE_PET || !has_project);
    }
    pet_view_set_mood(s_view, mood);
    ui_label_set(s_status, status);
}

void ui_pet_set(const char *project, proto_mood_t mood, const char *status)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply(project, mood, status);
    }
    ui_unlock();
}

void ui_pet_show_tabs(uint32_t ms)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        show_tabs(ms);
    }
    ui_unlock();
}

static void set_page(ui_pet_page_t page)
{
    if (page < UI_PET_PAGE_COUNT && page != s_page) {
        s_page = page;
        apply_page();
    }
}

void ui_pet_page_set(ui_pet_page_t page)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        set_page(page);
    }
    ui_unlock();
}

static void apply_about(const char *const values[UI_ABOUT_COUNT])
{
    for (int i = 0; i < UI_ABOUT_COUNT; i++) {
        ui_label_set(s_about_values[i], values[i]);
    }
    /* The last error is coral only when there is one. */
    const char *err = values[UI_ABOUT_ERROR];
    const bool error = err != NULL && err[0] != '\0' && strcmp(err, "-") != 0;
    if (error != s_error_shown) {
        s_error_shown = error;
        lv_obj_set_style_text_color(s_about_values[UI_ABOUT_ERROR], error ? UI_C_CORAL : UI_C_CREAM, 0);
    }
}

void ui_about_set(const char *const values[UI_ABOUT_COUNT])
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply_about(values);
    }
    ui_unlock();
}

void screen_pet_demo(ui_pet_demo_t demo)
{
    static const char *const ABOUT[UI_ABOUT_COUNT] = {
        [UI_ABOUT_NAME] = "devotchi-3F2A",
        [UI_ABOUT_FW] = "0.2.0",
        [UI_ABOUT_DEVICE_ID] = "dv-3f2a91",
        [UI_ABOUT_SSID] = "HomeNet-5G",
        [UI_ABOUT_RSSI] = "-58 dBm",
        [UI_ABOUT_IP] = "192.168.1.42",
        [UI_ABOUT_SERVER] = "ws://devotchi.local:8080",
        [UI_ABOUT_LINK] = "online (connected)",
        [UI_ABOUT_UPTIME] = "5h 02m 11s",
        [UI_ABOUT_RESET] = "power-on",
        [UI_ABOUT_ERROR] = "ws close 1006",
        [UI_ABOUT_NVS] = "ok",
    };
    lv_timer_pause(s_tabs_timer);
    s_tabs_on = false;
    ui_pet_page_t page = UI_PET_PAGE_PET;
    switch (demo) {
    case PET_DEMO_OFF:
        apply_tabs();
        return;
    case PET_DEMO_IDLE:
        apply("", MOOD_IDLE, "Waiting for an agent");
        break;
    case PET_DEMO_WORKING:
        apply("devotchi", MOOD_WORKING, "Edit src/routes/hooks.ts");
        break;
    case PET_DEMO_ANGRY:
        apply("api-gateway", MOOD_ANGRY, "pnpm test failed again in packages/server/test/ws.test.ts");
        break;
    case PET_DEMO_TABS:
        apply("", MOOD_IDLE, "Waiting for an agent");
        s_tabs_on = true;
        break;
    case PET_DEMO_HAPPY_BARE:
        apply("", MOOD_HAPPY, "");
        break;
    case PET_DEMO_SAD_BARE:
        apply("", MOOD_SAD, "");
        break;
    case PET_DEMO_IDLE_BARE:
        apply("", MOOD_IDLE, "");
        break;
    case PET_DEMO_ABOUT1:
    case PET_DEMO_ABOUT2:
        apply_about(ABOUT);
        page = demo == PET_DEMO_ABOUT1 ? UI_PET_PAGE_ABOUT : UI_PET_PAGE_ABOUT2;
        break;
    }
    if (page != s_page) {
        set_page(page);
    } else {
        apply_tabs();
    }
}

void screen_pet_set_banner(bool shown)
{
    pet_view_set_lift(s_view, shown ? BANNER_LIFT_PX : 0);
}

void ui_pet_react(pet_react_t r)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_react(s_view, r);
    ui_unlock();
}

void ui_pet_set_running(bool running)
{
    if (!ui_lock()) {
        return;
    }
    pet_views_set_powered(running);
    ui_unlock();
}

void ui_pet_freeze(bool frozen)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_set_frozen(s_view, frozen);
    ui_unlock();
}

void ui_pet_set_scale(uint8_t sixteenths)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_set_scale(s_view, sixteenths);
    ui_unlock();
}

void ui_pet_set_outline(bool on)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_set_outline(s_view, on);
    ui_unlock();
}

void ui_pet_get_state(ui_pet_state_t *out)
{
    *out = (ui_pet_state_t){0};
    if (!ui_lock()) {
        return;
    }
    pet_view_get_state(s_view, out);
    ui_unlock();
}
