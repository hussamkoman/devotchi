#include "app.h"
#include "app_tasks.h"
#include "cJSON.h"
#include "console.h"
#include "display.h"
#include "storage.h"
#include "ui.h"

#include "esp_err.h"
#include "esp_heap_caps.h"

/* cJSON allocates many small nodes; PSRAM keeps them out of the internal RAM that WiFi and DMA need. */
static void *json_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    return p != NULL ? p : heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}

static void json_free(void *p)
{
    heap_caps_free(p);
}

void app_main(void)
{
    cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&hooks);

    /* The console comes first so that every later failure is visible on it. */
    ESP_ERROR_CHECK(console_init(app_reset_reason()));
    const app_task_cfg_t console_task = APP_TASK_CONSOLE;
    ESP_ERROR_CHECK(console_start(console_task.name, console_task.stack, console_task.prio));

    /* A storage failure is reported by storage_is_degraded(); the device runs with defaults. */
    storage_init();

    ESP_ERROR_CHECK(display_init());
    display_set_brightness((uint8_t)storage_get_setting(STORAGE_SETTING_BRIGHTNESS));
    ESP_ERROR_CHECK(ui_init());
    const app_task_cfg_t ui_task = APP_TASK_UI;
    ESP_ERROR_CHECK(ui_start(ui_task.name, ui_task.stack, ui_task.prio));

    ESP_ERROR_CHECK(app_init());
    ESP_ERROR_CHECK(app_start());
}
