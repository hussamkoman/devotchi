#include "ui_priv.h"

#include "ui.h"
#include "ui_lockup.h"
#include "ui_logo.h"
#include "ui_theme.h"

#include "esp_app_desc.h"

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
/* About 1/3: the logo on the left, the lock-up and its lines in a column beside it, the source link across the bottom. */
#define CREDITS_LOGO_X   18
#define CREDITS_LOGO_Y   34
#define CREDITS_COL_X    (CREDITS_LOGO_X + UI_LOGO_W + 18)
#define CREDITS_COL_W    (LV_HOR_RES - CREDITS_COL_X - 12)
#define CREDITS_LOCKUP_Y 32
#define CREDITS_TAG_Y    (CREDITS_LOCKUP_Y + UI_LOCKUP_H + 4)
#define CREDITS_TEXT_Y   (CREDITS_TAG_Y + 22)
#define CREDITS_VER_Y    158
#define CREDITS_LINK_Y   184
#define CREDITS_LINK     "github.com/hussamkoman/devotchi"

static const char *const ABOUT_LABELS[UI_ABOUT_COUNT] = {
    [UI_ABOUT_FW] = "Firmware",    [UI_ABOUT_DEVICE] = "Device",     [UI_ABOUT_PET] = "Pet",
    [UI_ABOUT_WIFI] = "Wi-Fi",     [UI_ABOUT_IP] = "IP",             [UI_ABOUT_SERVER] = "Server",
    [UI_ABOUT_LINK] = "Link",      [UI_ABOUT_ROOM] = "Room",         [UI_ABOUT_BATTERY] = "Battery",
    [UI_ABOUT_UPTIME] = "Uptime",  [UI_ABOUT_RESET] = "Reset",       [UI_ABOUT_HEAP] = "Heap",
    [UI_ABOUT_FRAMES] = "Frames",  [UI_ABOUT_REQUESTS] = "Requests", [UI_ABOUT_PETS] = "Pets",
    [UI_ABOUT_ERROR] = "Last err", [UI_ABOUT_FONTS] = "Fonts",
};

/* Showcase caption (81, 82): a title and one line between the pet and the tabs. */
#define CAPTION_TITLE_Y  166
#define CAPTION_LINE_Y   196

/* Body centre of the home pet (mockups 10-16) and its scale in sixteenths. */
#define PET_CX           160
#define PET_CY           100
#define PET_SCALE        15
#define BANNER_LIFT_PX   4

static pet_view_t *s_view;
static lv_obj_t *s_project;
static lv_obj_t *s_status;
static lv_obj_t *s_about[2];
static lv_obj_t *s_about_main;
static lv_obj_t *s_credits;
static lv_obj_t *s_about_values[UI_ABOUT_COUNT];
static lv_obj_t *s_recent;
static lv_obj_t *s_recent_times[UI_RECENT_ROWS];
static lv_obj_t *s_recent_texts[UI_RECENT_ROWS];
static lv_obj_t *s_recent_empty;
static lv_obj_t *s_cap_title;
static lv_obj_t *s_cap_line;
static lv_timer_t *s_tabs_timer;

/* Guarded by the ui lock. */
static ui_pet_page_t s_page = UI_PET_PAGE_PET;
static bool s_shown;
static bool s_attention;
static bool s_tabs_on;
static bool s_boot_tabs_done;
static bool s_has_project;
static bool s_error_shown;
static bool s_caption;
static const char *s_cap_approve = "";
static char s_cap_bar[24];

static void apply_tabs(void)
{
    static const ui_tabs_t ANY = {.any = "Any button: OK"};
    static const ui_tabs_t HOME = {.tab = {
                                       [UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Poke"},
                                       [UI_TAB_ACTION] = {.state = UI_TAB_CLICK_AND_HOLD, .label = "Recent",
                                                          .hold = "Menu"},
                                       [UI_TAB_APPROVE] = {.state = UI_TAB_CLICK, .label = "Pat"},
                                   }};
    static const ui_tabs_t RECENT = {.tab = {[UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                                             [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Home"}}};
    static ui_tabs_t caption = {.tab = {[UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                                        [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Next"},
                                        [UI_TAB_APPROVE] = {.state = UI_TAB_CLICK}}};
    static const ui_tabs_t ABOUT = {.tab = {[UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                                            [UI_TAB_ACTION] = {.state = UI_TAB_CLICK, .label = "Page"}}};
    const ui_tabs_t *tabs = NULL;
    caption.tab[UI_TAB_APPROVE].label = s_cap_approve;
    if (s_attention) {
        tabs = &ANY;
    } else if (s_caption && s_page == UI_PET_PAGE_PET) {
        tabs = &caption;
    } else if (s_page == UI_PET_PAGE_RECENT) {
        tabs = &RECENT;
    } else if (s_page != UI_PET_PAGE_PET) {
        tabs = &ABOUT;
    } else if (s_tabs_on) {
        tabs = &HOME;
    }
    ui_screen_set_tabs(UI_SCREEN_PET, tabs);
}

static void apply_page(void)
{
    static const char *const TITLES[UI_PET_PAGE_COUNT] = {
        [UI_PET_PAGE_PET] = "",          [UI_PET_PAGE_RECENT] = "Recent",
        [UI_PET_PAGE_ABOUT] = "About 1/2", [UI_PET_PAGE_CREDITS] = "About 2/2",
        [UI_PET_PAGE_SYSTEM] = "System info 1/2", [UI_PET_PAGE_SYSTEM2] = "System info 2/2"};
    const bool pet = s_page == UI_PET_PAGE_PET;
    ui_set_hidden(s_about_main, s_page != UI_PET_PAGE_ABOUT);
    ui_set_hidden(s_credits, s_page != UI_PET_PAGE_CREDITS);
    ui_set_hidden(s_about[0], s_page != UI_PET_PAGE_SYSTEM);
    ui_set_hidden(s_about[1], s_page != UI_PET_PAGE_SYSTEM2);
    ui_set_hidden(s_recent, s_page != UI_PET_PAGE_RECENT);
    ui_set_hidden(s_project, !pet || !s_has_project || s_caption);
    ui_set_hidden(s_status, !pet || s_caption);
    ui_set_hidden(s_cap_title, !pet || !s_caption);
    ui_set_hidden(s_cap_line, !pet || !s_caption);
    ui_screen_set_title(UI_SCREEN_PET, s_caption && pet ? s_cap_bar : TITLES[s_page]);
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

static lv_obj_t *mask_image(lv_obj_t *parent, const lv_image_dsc_t *src, lv_color_t color, int32_t x, int32_t y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, x, y);
    return img;
}

static lv_obj_t *about_main_page(lv_obj_t *scr)
{
    lv_obj_t *page = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    mask_image(page, &ui_img_logo, UI_C_CREAM, CREDITS_LOGO_X, CREDITS_LOGO_Y);
    mask_image(page, &ui_img_lockup, UI_C_CREAM, CREDITS_COL_X, CREDITS_LOCKUP_Y);
    mask_image(page, &ui_img_lockup_tip, UI_C_MANGO, CREDITS_COL_X + UI_LOCKUP_TIP_X, CREDITS_LOCKUP_Y + UI_LOCKUP_TIP_Y);
    lv_obj_t *tag = ui_text(page, UI_FONT_SMALL, UI_C_MANGO, CREDITS_COL_X, CREDITS_TAG_Y, CREDITS_COL_W, true);
    lv_label_set_text_static(tag, "your agent's desk pet");
    lv_obj_t *what = ui_text(page, UI_FONT_SMALL, UI_C_FOG, CREDITS_COL_X, CREDITS_TEXT_Y, CREDITS_COL_W, false);
    lv_label_set_text_static(what, "Shows what your coding agent is doing. Answer it with three buttons.");
    char version[48];
    snprintf(version, sizeof(version), "Firmware v%s", esp_app_get_description()->version);
    lv_obj_t *ver = ui_text(page, UI_FONT_MONO, UI_C_SLATE, CREDITS_COL_X, CREDITS_VER_Y, CREDITS_COL_W, true);
    ui_label_set(ver, version);
    lv_obj_t *link = ui_text_line(page, UI_FONT_MONO, UI_C_CREAM, CREDITS_LINK_Y, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_static(link, CREDITS_LINK);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}

static lv_obj_t *credits_page(lv_obj_t *scr)
{
    static const char *const ROWS[][2] = {
        {"License", "MIT \xC2\xB7 2026 Hussam Koman"},
        {"Firmware", "ESP-IDF 6, LVGL 9, cJSON"},
        {"Server", "Node.js, Koa, SQLite"},
        {"Type", "Figtree, JetBrains Mono"},
        {"Logo", "The ghost on the PCB"},
    };
    const int rows = (int)(sizeof(ROWS) / sizeof(ROWS[0]));
    lv_obj_t *page = ui_box(scr, UI_TEXT_X, ABOUT_Y, UI_WIDTH_TEXT, rows * ABOUT_ROW_H);
    for (int i = 0; i < rows; i++) {
        const int32_t y = i * ABOUT_ROW_H;
        lv_obj_t *label = ui_text(page, UI_FONT_SMALL, UI_C_FOG, 0, y + 1, ABOUT_LABEL_W, true);
        lv_label_set_text_static(label, ROWS[i][0]);
        lv_obj_t *value = ui_text(page, UI_FONT_SMALL, UI_C_CREAM, ABOUT_LABEL_W, y + 1, ABOUT_VALUE_W, true);
        lv_label_set_text_static(value, ROWS[i][1]);
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
    s_about_main = about_main_page(scr);
    s_credits = credits_page(scr);
    s_about[0] = about_page(scr, 0, UI_ABOUT_PAGE1_ROWS);
    s_about[1] = about_page(scr, UI_ABOUT_PAGE1_ROWS, UI_ABOUT_COUNT);
    s_recent = ui_box(scr, UI_TEXT_X, ABOUT_Y, UI_WIDTH_TEXT, UI_RECENT_ROWS * ABOUT_ROW_H * 3 / 2);
    for (int i = 0; i < UI_RECENT_ROWS; i++) {
        const int32_t y = i * ABOUT_ROW_H * 3 / 2;
        s_recent_times[i] = ui_text(s_recent, UI_FONT_MONO, UI_C_FOG, 0, y + 1, ABOUT_LABEL_W - 8, true);
        s_recent_texts[i] = ui_text(s_recent, UI_FONT_SMALL, UI_C_CREAM, ABOUT_LABEL_W - 8, y, ABOUT_VALUE_W + 8, true);
    }
    s_recent_empty = ui_text(s_recent, UI_FONT_SMALL, UI_C_FOG, 0, 1, UI_WIDTH_TEXT, true);
    lv_label_set_text_static(s_recent_empty, "Nothing from the agent yet");
    lv_obj_add_flag(s_recent, LV_OBJ_FLAG_HIDDEN);
    s_cap_title = ui_text_line(scr, UI_FONT_TITLE, UI_C_CREAM, CAPTION_TITLE_Y, LV_TEXT_ALIGN_CENTER);
    s_cap_line = ui_text_line(scr, UI_FONT_SMALL, UI_C_FOG, CAPTION_LINE_Y, LV_TEXT_ALIGN_CENTER);
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

static void apply_recent(const char *const times[UI_RECENT_ROWS], const char *const texts[UI_RECENT_ROWS], uint8_t n)
{
    for (uint8_t i = 0; i < UI_RECENT_ROWS; i++) {
        ui_set_hidden(s_recent_times[i], i >= n);
        ui_set_hidden(s_recent_texts[i], i >= n);
        ui_label_set(s_recent_times[i], i < n ? times[i] : "");
        ui_label_set(s_recent_texts[i], i < n ? texts[i] : "");
    }
    ui_set_hidden(s_recent_empty, n > 0);
}

void ui_recent_set(const char *const times[UI_RECENT_ROWS], const char *const texts[UI_RECENT_ROWS], uint8_t n)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply_recent(times, texts, n);
    }
    ui_unlock();
}

static void apply_caption(const char *bar, const char *title, const char *line, const char *approve)
{
    s_caption = title != NULL;
    strlcpy(s_cap_bar, s_caption && bar != NULL ? bar : "", sizeof(s_cap_bar));
    ui_label_set(s_cap_title, s_caption ? title : "");
    ui_label_set(s_cap_line, s_caption && line != NULL ? line : "");
    s_cap_approve = approve != NULL ? approve : "";
    apply_page();
}

void ui_pet_set_caption(const char *bar, const char *title, const char *line, const char *approve)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active()) {
        apply_caption(bar, title, line, approve);
    }
    ui_unlock();
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
        [UI_ABOUT_FW] = "2.1.0 \xC2\xB7 a1b2c3d",
        [UI_ABOUT_DEVICE] = "devotchi-3F2A",
        [UI_ABOUT_PET] = "Devo (built in)",
        [UI_ABOUT_WIFI] = "HomeNet-5G -58 dBm",
        [UI_ABOUT_IP] = "192.168.1.42",
        [UI_ABOUT_SERVER] = "devotchi.local",
        [UI_ABOUT_LINK] = "online 2 h 14 min",
        [UI_ABOUT_ROOM] = "desk \xC2\xB7 mode ask",
        [UI_ABOUT_BATTERY] = "3.92 V 72 % charging",
        [UI_ABOUT_UPTIME] = "5 h 02 min",
        [UI_ABOUT_RESET] = "power-on",
        [UI_ABOUT_HEAP] = "148 kB free \xC2\xB7 min 96",
        [UI_ABOUT_FRAMES] = "30 fps \xC2\xB7 11 ms draw",
        [UI_ABOUT_REQUESTS] = "42 held \xC2\xB7 37 answered",
        [UI_ABOUT_PETS] = "2 of 8 \xC2\xB7 41 kB free",
        [UI_ABOUT_ERROR] = "ws close 1006, 14:02",
        [UI_ABOUT_FONTS] = "Figtree, JB Mono (OFL)",
    };
    static const char *const TIMES[UI_RECENT_ROWS] = {"14:32", "14:30", "14:21", "14:02", "13:58"};
    static const char *const TEXTS[UI_RECENT_ROWS] = {
        "Tests passed", "Edit src/routes/hooks.ts", "Build failed", "Reading packages/server", "Task complete"};
    lv_timer_pause(s_tabs_timer);
    s_tabs_on = false;
    s_caption = false;
    ui_pet_page_t page = UI_PET_PAGE_PET;
    switch (demo) {
    case PET_DEMO_OFF:
        apply_caption(NULL, NULL, NULL, NULL);
        return;
    case PET_DEMO_RECENT:
        apply_recent(TIMES, TEXTS, UI_RECENT_ROWS);
        page = UI_PET_PAGE_RECENT;
        break;
    case PET_DEMO_MOODS:
        apply("", MOOD_PROUD, "");
        apply_caption("Moods 9/11", "Proud", "All green after a big refactor", "Pat");
        break;
    case PET_DEMO_REACTIONS:
        apply("", MOOD_IDLE, "");
        apply_caption("Reactions", "Dizzy", "Three pokes within two seconds", "Replay");
        break;
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
        page = demo == PET_DEMO_ABOUT1 ? UI_PET_PAGE_SYSTEM : UI_PET_PAGE_SYSTEM2;
        break;
    }
    if (page != s_page) {
        set_page(page);
    } else {
        apply_page();
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
