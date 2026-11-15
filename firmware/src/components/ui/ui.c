#include "ui.h"

#include "display.h"
#include "text.h"
#include "ui_priv.h"
#include "ui_theme.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "ui";

#define UI_LOCK_TIMEOUT_MS  1000
#define UI_MIN_SLEEP_MS     1
#define UI_MAX_SLEEP_MS     500
#define FRAME_HISTORY       32

static const char *const SCREEN_NAMES[UI_SCREEN_COUNT] = {
    [UI_SCREEN_SPLASH] = "splash",
    [UI_SCREEN_PROVISIONING] = "provisioning",
    [UI_SCREEN_WIFI] = "wifi",
    [UI_SCREEN_CONNECTING] = "connecting",
    [UI_SCREEN_PAIRING] = "pairing",
    [UI_SCREEN_PET] = "pet",
    [UI_SCREEN_APPROVAL] = "approval",
    [UI_SCREEN_QUESTION] = "question",
    [UI_SCREEN_PLAN] = "plan",
    [UI_SCREEN_SETTINGS] = "settings",
    [UI_SCREEN_PICKER] = "picker",
    [UI_SCREEN_IDENTIFY] = "identify",
    [UI_SCREEN_LOWBATT] = "lowbatt",
    [UI_SCREEN_CLOCK] = "clock",
    [UI_SCREEN_SLEEP] = "sleep",
};

static TaskHandle_t s_task;
static atomic_bool s_running = true;

/* Everything below is guarded by the ui lock. */
static lv_obj_t *s_screens[UI_SCREEN_COUNT];
static ui_screen_id_t s_current = UI_SCREEN_SPLASH;
/** The screen the app asked for; differs from s_current only during ui demo. */
static ui_screen_id_t s_app_screen = UI_SCREEN_SPLASH;
static ui_tabs_t s_tabs[UI_SCREEN_COUNT];
static const char *s_titles[UI_SCREEN_COUNT];
static bool s_demo;
static uint16_t s_frame_ms_x10[FRAME_HISTORY];
static uint32_t s_frames;

bool ui_lock(void)
{
    return display_lock(UI_LOCK_TIMEOUT_MS);
}

void ui_unlock(void)
{
    display_unlock();
    /* LVGL pauses its refresh timer while nothing is dirty; wake the task to pick up the change. */
    if (s_task != NULL && xTaskGetCurrentTaskHandle() != s_task) {
        xTaskNotifyGive(s_task);
    }
}

lv_obj_t *ui_screen_base_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_C_INK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, UI_C_CREAM, 0);
    lv_obj_set_style_text_font(scr, UI_FONT_SMALL, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color, int32_t radius)
{
    lv_obj_t *o = ui_box(parent, x, y, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t x, int32_t y, int32_t w,
                  bool one_line)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    if (w > 0) {
        lv_obj_set_width(label, w);
    }
    if (one_line) {
        lv_obj_set_height(label, lv_font_get_line_height(font));
        lv_label_set_long_mode(label, w > 0 ? LV_LABEL_LONG_MODE_DOTS : LV_LABEL_LONG_MODE_CLIP);
    } else {
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    }
    lv_label_set_text_static(label, "");
    lv_obj_set_pos(label, x, y);
    return label;
}

lv_obj_t *ui_text_line(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t y, lv_text_align_t align)
{
    lv_obj_t *label = ui_text(parent, font, color, UI_TEXT_X, y, UI_WIDTH_TEXT, true);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

lv_obj_t *ui_icon(lv_obj_t *parent, const char *icon, lv_color_t color, int32_t x, int32_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, UI_FONT_ICONS, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text_static(label, icon);
    lv_obj_set_pos(label, x, y);
    lv_obj_add_flag(label, UI_FLAG_NO_TEXT);
    return label;
}

void ui_label_set(lv_obj_t *label, const char *text)
{
    /* Static: callers hold the ui lock, and a details page does not belong on a task stack. */
    static char clean[UI_LABEL_TEXT_SIZE];
    text_sanitize_device(clean, sizeof(clean), text != NULL ? text : "");
    if (strcmp(lv_label_get_text(label), clean) != 0) {
        lv_label_set_text(label, clean);
    }
}

void ui_host_text(char *dst, size_t cap, const char *url, size_t cols)
{
    char host[UI_TEXT_SIZE];
    const char *start = url != NULL ? strstr(url, "://") : NULL;
    strlcpy(host, start != NULL ? start + 3 : url != NULL ? url : "", sizeof(host));
    char *path = strchr(host, '/');
    if (path != NULL) {
        *path = '\0';
    }
    const size_t len = strlen(host);
    static const char DEFAULT_PORT[] = ":8080";
    if (len > sizeof(DEFAULT_PORT) - 1 && strcmp(host + len - (sizeof(DEFAULT_PORT) - 1), DEFAULT_PORT) == 0) {
        host[len - (sizeof(DEFAULT_PORT) - 1)] = '\0';
    }
    text_truncate_middle(dst, cap, host, cols);
}

void ui_label_set_caps(lv_obj_t *label, const char *text)
{
    static char upper[UI_TEXT_SIZE];
    text_upper_device(upper, sizeof(upper), text != NULL ? text : "");
    ui_label_set(label, upper);
}

void ui_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden != lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void record_frame(int64_t us)
{
    const int64_t ms_x10 = us / 100;
    s_frame_ms_x10[s_frames % FRAME_HISTORY] = ms_x10 > UINT16_MAX ? UINT16_MAX : (uint16_t)ms_x10;
    s_frames++;
}

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!atomic_load(&s_running)) {
            /* Parked: notifications from ui_unlock() only bring the task back here. */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        uint32_t sleep_ms = UI_MIN_SLEEP_MS;
        if (ui_lock()) {
            const uint32_t flushed = display_flush_count();
            const int64_t start = esp_timer_get_time();
            sleep_ms = lv_timer_handler();
            /* The last strip is still on the wire; a frame ends when the panel has it. */
            display_flush_wait();
            if (display_flush_count() != flushed) {
                record_frame(esp_timer_get_time() - start);
            }
            display_unlock();
        }
        if (sleep_ms < UI_MIN_SLEEP_MS) {
            sleep_ms = UI_MIN_SLEEP_MS;
        } else if (sleep_ms > UI_MAX_SLEEP_MS) {
            sleep_ms = UI_MAX_SLEEP_MS;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms));
    }
}

static bool shows_status_bar(ui_screen_id_t id)
{
    return id != UI_SCREEN_SPLASH && id != UI_SCREEN_LOWBATT && id != UI_SCREEN_IDENTIFY && id != UI_SCREEN_CLOCK;
}

/* Caller holds the ui lock. */
static void load(ui_screen_id_t id)
{
    s_current = id;
    lv_screen_load(s_screens[id]);
    screen_pet_set_shown(id == UI_SCREEN_PET);
    screen_picker_set_shown(id == UI_SCREEN_PICKER);
    screen_clock_set_shown(id == UI_SCREEN_CLOCK);
    screen_sleep_set_shown(id == UI_SCREEN_SLEEP);
    w_status_set_visible(shows_status_bar(id));
    w_status_set_title(s_titles[id] != NULL ? s_titles[id] : "");
    w_tabs_set(&s_tabs[id]);
}

void ui_screen_set_tabs(ui_screen_id_t id, const ui_tabs_t *tabs)
{
    if (id >= UI_SCREEN_COUNT) {
        return;
    }
    if (tabs != NULL) {
        s_tabs[id] = *tabs;
    } else {
        memset(&s_tabs[id], 0, sizeof(s_tabs[id]));
    }
    if (id == s_current) {
        w_tabs_set(&s_tabs[id]);
    }
}

void ui_screen_set_title(ui_screen_id_t id, const char *title)
{
    if (id >= UI_SCREEN_COUNT) {
        return;
    }
    s_titles[id] = title;
    if (id == s_current) {
        w_status_set_title(title != NULL ? title : "");
    }
}

bool ui_demo_active(void)
{
    return s_demo;
}

esp_err_t ui_init(void)
{
    lv_tick_set_cb(tick_ms);

    ESP_RETURN_ON_FALSE(ui_lock(), ESP_ERR_TIMEOUT, TAG, "lock");
    ui_theme_init(lv_display_get_default());
    s_screens[UI_SCREEN_SPLASH] = screen_splash_create();
    s_screens[UI_SCREEN_PROVISIONING] = screen_provisioning_create();
    s_screens[UI_SCREEN_WIFI] = screen_wifi_create();
    s_screens[UI_SCREEN_CONNECTING] = screen_connecting_create();
    s_screens[UI_SCREEN_PAIRING] = screen_pairing_create();
    s_screens[UI_SCREEN_PET] = screen_pet_create();
    s_screens[UI_SCREEN_APPROVAL] = screen_approval_create();
    s_screens[UI_SCREEN_QUESTION] = screen_question_create();
    s_screens[UI_SCREEN_PLAN] = screen_plan_create();
    s_screens[UI_SCREEN_IDENTIFY] = screen_identify_create();
    s_screens[UI_SCREEN_SETTINGS] = screen_settings_create();
    s_screens[UI_SCREEN_PICKER] = screen_picker_create();
    s_screens[UI_SCREEN_LOWBATT] = screen_lowbatt_create();
    s_screens[UI_SCREEN_CLOCK] = screen_clock_create();
    s_screens[UI_SCREEN_SLEEP] = screen_sleep_create();
    /* Creation order is the stacking order on the top layer: tabs above toasts, both above the bar. */
    w_status_create();
    w_toast_create();
    w_tabs_create();
    s_app_screen = UI_SCREEN_SPLASH;
    load(UI_SCREEN_SPLASH);
    display_unlock();
    return ESP_OK;
}

esp_err_t ui_start(const char *task_name, uint32_t stack_size, uint32_t priority)
{
    const BaseType_t ok = xTaskCreate(ui_task, task_name, stack_size, NULL, priority, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void ui_set_running(bool running)
{
    atomic_store(&s_running, running);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

bool ui_is_running(void)
{
    return atomic_load(&s_running);
}

const char *ui_screen_name(ui_screen_id_t id)
{
    return id < UI_SCREEN_COUNT ? SCREEN_NAMES[id] : "";
}

esp_err_t ui_show_screen(ui_screen_id_t id)
{
    if (id >= UI_SCREEN_COUNT || s_screens[id] == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!ui_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    s_app_screen = id;
    if (!s_demo && id != s_current) {
        load(id);
    }
    ui_unlock();
    return ESP_OK;
}

ui_screen_id_t ui_current_screen(void)
{
    ui_screen_id_t id = UI_SCREEN_SPLASH;
    if (ui_lock()) {
        id = s_current;
        display_unlock();
    }
    return id;
}

const char *ui_current_screen_name(void)
{
    return ui_screen_name(ui_current_screen());
}

/* ---- ui demo ---- */

typedef struct {
    const char *name;
    /** "|"-separated variants, the first is the default; NULL for none. */
    const char *variants;
    ui_screen_id_t screen;
} demo_entry_t;

static const demo_entry_t DEMOS[] = {
    {"splash", NULL, UI_SCREEN_SPLASH},
    {"provisioning", "qr|code", UI_SCREEN_PROVISIONING},
    {"wifi", NULL, UI_SCREEN_WIFI},
    {"connecting", NULL, UI_SCREEN_CONNECTING},
    {"offline", NULL, UI_SCREEN_CONNECTING},
    {"pairing", NULL, UI_SCREEN_PAIRING},
    {"home", "idle|working|angry|tabs", UI_SCREEN_PET},
    {"toast", "positive|sticky|desk|battery|strip|receipt", UI_SCREEN_PET},
    {"about", "1|2", UI_SCREEN_PET},
    {"recent", NULL, UI_SCREEN_PET},
    {"approval", "low|med|details|long|high|clicked|holding|sent|next", UI_SCREEN_APPROVAL},
    {"menu", "terminal|session|high|locked", UI_SCREEN_APPROVAL},
    {"tabs", "hold|nudge|any|inactive", UI_SCREEN_APPROVAL},
    {"question", "single|second|back|read|scroll|multi|send|menu|review|accents|accents-review", UI_SCREEN_QUESTION},
    {"plan", "card|menu|cut|more|accents", UI_SCREEN_PLAN},
    {"settings", "top|display|editor|confirm|quick", UI_SCREEN_SETTINGS},
    {"showcase", "menu|moods|reactions|sample|real", UI_SCREEN_SETTINGS},
    {"clock", "day|night|notime", UI_SCREEN_CLOCK},
    {"sleep", NULL, UI_SCREEN_SLEEP},
    {"picker", "devo|ember|more", UI_SCREEN_PICKER},
    {"identify", NULL, UI_SCREEN_IDENTIFY},
    {"lowbatt", NULL, UI_SCREEN_LOWBATT},
};

const char *ui_demo_usage(void)
{
    return "splash provisioning[qr|code] wifi connecting offline pairing home[idle|working|angry|tabs] "
           "toast[positive|sticky|desk|battery|strip|receipt] about[1|2] "
           "approval[low|med|details|long|high|clicked|holding|sent|next] menu[terminal|session|high|locked] "
           "tabs[hold|nudge|any|inactive] question[single|second|back|read|scroll|multi|send|menu|review|accents|"
           "accents-review] plan[card|menu|cut|more|accents] settings[top|display|editor|confirm|quick] "
           "showcase[menu|moods|reactions|sample|real] clock[day|night|notime] sleep recent picker[devo|ember|more] "
           "identify lowbatt | off";
}

/* Index of variant in the "|"-separated list, or -1. NULL or "" picks the first. */
static int variant_index(const char *list, const char *variant)
{
    if (variant == NULL || variant[0] == '\0') {
        return 0;
    }
    if (list == NULL) {
        return -1;
    }
    const size_t len = strlen(variant);
    int index = 0;
    for (const char *p = list; *p != '\0'; index++) {
        const char *end = strchr(p, '|');
        const size_t n = end != NULL ? (size_t)(end - p) : strlen(p);
        if (n == len && strncmp(p, variant, n) == 0) {
            return index;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return -1;
}

static void demo_status(bool desk, uint8_t pct, ui_link_t link, uint8_t bars)
{
    const ui_status_t st = {
        .battery = UI_BATT_DISCHARGING,
        .pct = pct,
        .low = pct <= 15,
        .desk = desk,
        .rssi_bars = bars,
        .link = link,
    };
    w_status_demo(&st);
}

static void demo_load(const demo_entry_t *d, int v)
{
    static const ui_pet_demo_t HOME[] = {PET_DEMO_IDLE, PET_DEMO_WORKING, PET_DEMO_ANGRY, PET_DEMO_TABS};
    static const ui_pet_demo_t TOAST[] = {PET_DEMO_HAPPY_BARE, PET_DEMO_SAD_BARE, PET_DEMO_IDLE_BARE,
                                          PET_DEMO_IDLE_BARE, PET_DEMO_IDLE_BARE, PET_DEMO_IDLE_BARE};
    w_toast_demo(W_TOAST_DEMO_NONE);
    demo_status(false, 72, UI_LINK_REGISTERED, 3);
    switch (d->screen) {
    case UI_SCREEN_PROVISIONING:
        demo_status(false, 72, UI_LINK_OFFLINE, 0);
        screen_provisioning_demo(v);
        break;
    case UI_SCREEN_WIFI:
        demo_status(false, 72, UI_LINK_OFFLINE, 1);
        screen_wifi_demo();
        break;
    case UI_SCREEN_CONNECTING: {
        const bool offline = strcmp(d->name, "offline") == 0;
        demo_status(false, 72, offline ? UI_LINK_OFFLINE : UI_LINK_CONNECTING, 3);
        screen_connecting_demo(offline);
        break;
    }
    case UI_SCREEN_PAIRING:
        demo_status(false, 72, UI_LINK_CONNECTING, 3);
        screen_pairing_demo();
        break;
    case UI_SCREEN_PET:
        if (strcmp(d->name, "toast") == 0) {
            demo_status(v == 2, v == 3 ? 12 : 72, UI_LINK_REGISTERED, 3);
            screen_pet_demo(TOAST[v]);
            w_toast_demo((w_toast_demo_t)v);
        } else if (strcmp(d->name, "recent") == 0) {
            screen_pet_demo(PET_DEMO_RECENT);
        } else if (strcmp(d->name, "about") == 0) {
            screen_pet_demo(v == 0 ? PET_DEMO_ABOUT1 : PET_DEMO_ABOUT2);
        } else {
            screen_pet_demo(HOME[v]);
        }
        break;
    case UI_SCREEN_APPROVAL:
        screen_approval_demo(strcmp(d->name, "tabs") == 0   ? APPROVAL_DEMO_HOLD + v
                             : strcmp(d->name, "menu") == 0 ? APPROVAL_DEMO_MENU + v
                                                            : APPROVAL_DEMO_LOW + v);
        break;
    case UI_SCREEN_QUESTION:
        screen_question_demo(v);
        break;
    case UI_SCREEN_PLAN:
        screen_plan_demo(v);
        break;
    case UI_SCREEN_SETTINGS:
        screen_settings_demo(v);
        break;
    case UI_SCREEN_CLOCK:
        screen_clock_demo(v);
        break;
    case UI_SCREEN_SLEEP:
        screen_sleep_demo();
        break;
    case UI_SCREEN_PICKER:
        screen_picker_demo(v);
        break;
    case UI_SCREEN_IDENTIFY:
        screen_identify_demo();
        break;
    case UI_SCREEN_SPLASH:
    case UI_SCREEN_LOWBATT:
    case UI_SCREEN_COUNT:
        break;
    }
}

/* The showcase pages are other screens under the violet mark (80-85). */
static const demo_entry_t SHOWCASE_SCREENS[] = {
    {"menu", NULL, UI_SCREEN_SETTINGS},
    {"moods", NULL, UI_SCREEN_PET},
    {"reactions", NULL, UI_SCREEN_PET},
    {"sample", NULL, UI_SCREEN_APPROVAL},
    {"real", NULL, UI_SCREEN_APPROVAL},
};

static void demo_showcase(int v)
{
    w_toast_demo(W_TOAST_DEMO_NONE);
    demo_status(false, 72, UI_LINK_REGISTERED, 3);
    load(SHOWCASE_SCREENS[v].screen);
    w_status_set_mark(v == 3 ? UI_MARK_SAMPLE : v == 4 ? UI_MARK_REAL : UI_MARK_DEMO);
    switch (v) {
    case 0:
        screen_settings_demo(5);
        break;
    case 1:
        screen_pet_demo(PET_DEMO_MOODS);
        break;
    case 2:
        screen_pet_demo(PET_DEMO_REACTIONS);
        break;
    case 3:
        screen_approval_demo(APPROVAL_DEMO_HIGH);
        break;
    default:
        screen_approval_demo(APPROVAL_DEMO_REAL);
        break;
    }
}

esp_err_t ui_demo(const char *name, const char *variant)
{
    const demo_entry_t *d = NULL;
    for (size_t i = 0; i < sizeof(DEMOS) / sizeof(DEMOS[0]); i++) {
        if (strcmp(DEMOS[i].name, name) == 0) {
            d = &DEMOS[i];
        }
    }
    const int v = d != NULL ? variant_index(d->variants, variant) : -1;
    if (v < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!ui_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    s_demo = true;
    if (strcmp(d->name, "showcase") == 0) {
        demo_showcase(v);
    } else {
        w_status_set_mark(UI_MARK_NONE);
        load(d->screen);
        demo_load(d, v);
    }
    ui_unlock();
    return ESP_OK;
}

void ui_demo_off(void)
{
    if (!ui_lock()) {
        return;
    }
    if (s_demo) {
        s_demo = false;
        screen_pet_demo(PET_DEMO_OFF);
        screen_approval_demo(APPROVAL_DEMO_OFF);
        screen_provisioning_demo(-1);
        w_toast_demo(W_TOAST_DEMO_NONE);
        w_status_demo(NULL);
        w_status_set_mark(w_status_app_mark());
        load(s_app_screen);
    }
    ui_unlock();
}

bool ui_demo_is_active(void)
{
    bool active = false;
    if (ui_lock()) {
        active = s_demo;
        ui_unlock();
    }
    return active;
}

/* ---- texts ---- */

typedef struct {
    char (*texts)[UI_TEXT_SIZE];
    size_t max;
    size_t count;
} text_walk_t;

/* Removes LVGL recolour commands ("#RRGGBB text#", "##" for '#') in place, leaving the text a person reads. */
static void strip_recolor(char *text)
{
    char *dst = text;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p != '#') {
            *dst++ = *p;
        } else if (p[1] == '#') {
            *dst++ = '#';
            p++;
        } else if (strspn(p + 1, "0123456789abcdefABCDEF") >= 6 && p[7] == ' ') {
            p += 7;
        }
    }
    *dst = '\0';
}

static lv_obj_tree_walk_res_t collect_text(lv_obj_t *obj, void *user_data)
{
    text_walk_t *walk = user_data;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_flag(obj, UI_FLAG_NO_TEXT)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        if (text != NULL && text[0] != '\0') {
            if (walk->count >= walk->max) {
                return LV_OBJ_TREE_WALK_END;
            }
            const char *shown = lv_obj_has_flag(obj, UI_FLAG_SECRET) ? UI_TEXT_HIDDEN : text;
            char *out = walk->texts[walk->count++];
            text_utf8_truncate(out, UI_TEXT_SIZE, shown);
            if (lv_label_get_recolor(obj)) {
                strip_recolor(out);
            }
        }
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

size_t ui_get_texts(char texts[][UI_TEXT_SIZE], size_t max_texts)
{
    text_walk_t walk = {.texts = texts, .max = max_texts, .count = 0};
    if (!ui_lock()) {
        return 0;
    }
    lv_obj_tree_walk(lv_screen_active(), collect_text, &walk);
    lv_obj_tree_walk(lv_layer_top(), collect_text, &walk);
    display_unlock();
    return walk.count;
}

uint32_t ui_frame_recent_x10(void)
{
    const uint32_t n = s_frames < UI_RECENT_FRAMES ? s_frames : UI_RECENT_FRAMES;
    uint32_t sum = 0;
    for (uint32_t i = 1; i <= n; i++) {
        sum += s_frame_ms_x10[(s_frames - i) % FRAME_HISTORY];
    }
    return n > 0 ? sum / n : 0;
}

void ui_get_stats(ui_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!ui_lock()) {
        return;
    }
    const uint32_t n = s_frames < FRAME_HISTORY ? s_frames : FRAME_HISTORY;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += s_frame_ms_x10[i];
        if (s_frame_ms_x10[i] > out->max_ms_x10) {
            out->max_ms_x10 = s_frame_ms_x10[i];
        }
    }
    out->frames = s_frames;
    out->avg_ms_x10 = n > 0 ? sum / n : 0;

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    out->lvgl_used_pct = mon.used_pct;
    display_unlock();
}
