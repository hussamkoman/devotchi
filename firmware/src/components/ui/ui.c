#include "ui.h"

#include "display.h"
#include "ui_priv.h"
#include "ui_theme.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <string.h>

static const char *TAG = "ui";

#define UI_LOCK_TIMEOUT_MS  1000
#define UI_MIN_SLEEP_MS     1
#define UI_MAX_SLEEP_MS     500
#define FRAME_HISTORY       32

static const char *const SCREEN_NAMES[UI_SCREEN_COUNT] = {
    [UI_SCREEN_SPLASH] = "splash",
    [UI_SCREEN_DIAG] = "diag",
    [UI_SCREEN_PROVISIONING] = "provisioning",
    [UI_SCREEN_WIFI] = "wifi",
    [UI_SCREEN_CONNECTING] = "connecting",
    [UI_SCREEN_PAIRING] = "pairing",
    [UI_SCREEN_PET] = "pet",
    [UI_SCREEN_APPROVAL] = "approval",
    [UI_SCREEN_SETTINGS] = "settings",
    [UI_SCREEN_IDENTIFY] = "identify",
    [UI_SCREEN_LOWBATT] = "lowbatt",
};

static TaskHandle_t s_task;

/* Everything below is guarded by the ui lock. */
static lv_obj_t *s_screens[UI_SCREEN_COUNT];
static ui_screen_id_t s_current = UI_SCREEN_SPLASH;
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
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
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
        uint32_t sleep_ms = UI_MIN_SLEEP_MS;
        if (ui_lock()) {
            const uint32_t flushed = display_flush_count();
            const int64_t start = esp_timer_get_time();
            sleep_ms = lv_timer_handler();
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

esp_err_t ui_init(void)
{
    lv_tick_set_cb(tick_ms);

    ESP_RETURN_ON_FALSE(ui_lock(), ESP_ERR_TIMEOUT, TAG, "lock");
    s_screens[UI_SCREEN_SPLASH] = screen_splash_create();
    s_screens[UI_SCREEN_DIAG] = screen_diag_create();
    s_current = UI_SCREEN_SPLASH;
    lv_screen_load(s_screens[UI_SCREEN_SPLASH]);
    display_unlock();
    return ESP_OK;
}

esp_err_t ui_start(const char *task_name, uint32_t stack_size, uint32_t priority)
{
    const BaseType_t ok = xTaskCreate(ui_task, task_name, stack_size, NULL, priority, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
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
    if (id != s_current) {
        s_current = id;
        lv_screen_load(s_screens[id]);
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

typedef struct {
    char (*texts)[UI_TEXT_SIZE];
    size_t max;
    size_t count;
} text_walk_t;

static lv_obj_tree_walk_res_t collect_text(lv_obj_t *obj, void *user_data)
{
    text_walk_t *walk = user_data;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        if (text != NULL && text[0] != '\0') {
            if (walk->count >= walk->max) {
                return LV_OBJ_TREE_WALK_END;
            }
            strlcpy(walk->texts[walk->count++], text, UI_TEXT_SIZE);
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
