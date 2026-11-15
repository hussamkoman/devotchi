#include "display.h"

#include "board_pins.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "display";

#define BYTES_PER_PIXEL          2
#define DRAW_BUF_LINES           40
#define DRAW_BUF_SIZE            (BOARD_DISP_H_RES * DRAW_BUF_LINES * BYTES_PER_PIXEL)
#define FRAME_SIZE               (BOARD_DISP_H_RES * BOARD_DISP_V_RES * BYTES_PER_PIXEL)
#define FLUSH_TIMEOUT_MS         100
#define PANEL_LOCK_TIMEOUT_MS    500
#define SNAPSHOT_LOCK_TIMEOUT_MS 1000
#define SNAPSHOT_SETTLE_MS       150
#define SNAPSHOT_POLL_MS         10

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_trans_done;
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;

static atomic_uint s_flush_errors;
static atomic_uint s_flush_count;

/* Backlight state; guarded by s_lock. */
static uint8_t s_brightness = 75;
static bool s_frame_shown;
static bool s_asleep;

#if CONFIG_DEVOTCHI_SHADOW_FB
static uint8_t *s_shadow;
static uint8_t *s_snapshot;
#endif

static void set_backlight(uint8_t percent)
{
    const uint32_t duty = (uint32_t)percent * BOARD_LEDC_DUTY_MAX / 100U;
    ledc_set_duty(BOARD_LEDC_MODE, BOARD_LEDC_CH_BACKLIGHT, duty);
    ledc_update_duty(BOARD_LEDC_MODE, BOARD_LEDC_CH_BACKLIGHT);
}

/* Caller holds s_lock. */
static void apply_backlight(void)
{
    set_backlight(s_frame_shown && !s_asleep ? s_brightness : 0);
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &woken);
    return woken == pdTRUE;
}

/* Runs inside lv_timer_handler(), so s_lock is held. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
#if CONFIG_DEVOTCHI_SHADOW_FB
    const size_t row_bytes = (size_t)lv_area_get_width(area) * BYTES_PER_PIXEL;
    const uint8_t *src = px_map;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        memcpy(s_shadow + ((size_t)y * BOARD_DISP_H_RES + (size_t)area->x1) * BYTES_PER_PIXEL, src,
               row_bytes);
        src += row_bytes;
    }
#endif

    /* A completion that arrived after an earlier timeout must not satisfy this transfer. */
    xSemaphoreTake(s_trans_done, 0);

    const esp_err_t err =
        esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (err != ESP_OK) {
        atomic_fetch_add(&s_flush_errors, 1);
    } else if (xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(FLUSH_TIMEOUT_MS)) != pdTRUE) {
        atomic_fetch_add(&s_flush_errors, 1);
    }
    atomic_fetch_add(&s_flush_count, 1);

    if (!s_frame_shown && lv_display_flush_is_last(disp)) {
        s_frame_shown = true;
        apply_backlight();
    }
    lv_display_flush_ready(disp);
}

static esp_err_t init_backlight(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BOARD_LEDC_MODE,
        .duty_resolution = BOARD_LEDC_DUTY_BITS,
        .timer_num = BOARD_LEDC_TIMER_BACKLIGHT,
        .freq_hz = BOARD_LEDC_FREQ_HZ,
        .clk_cfg = BOARD_LEDC_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");

    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_PIN_DISP_BL,
        .speed_mode = BOARD_LEDC_MODE,
        .channel = BOARD_LEDC_CH_BACKLIGHT,
        .timer_sel = BOARD_LEDC_TIMER_BACKLIGHT,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    };
    return ledc_channel_config(&channel);
}

static esp_err_t init_panel(void)
{
    const spi_bus_config_t bus = {
        .mosi_io_num = BOARD_PIN_DISP_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_PIN_DISP_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = DRAW_BUF_SIZE,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_DISP_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_PIN_DISP_CS,
        .dc_gpio_num = BOARD_PIN_DISP_DC,
        .spi_mode = 0,
        .pclk_hz = CONFIG_DEVOTCHI_DISPLAY_SPI_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_DISP_SPI_HOST, &io_cfg, &io), TAG,
        "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .reset_gpio_num = BOARD_PIN_DISP_RST,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel), TAG, "st7789");

    /* Orientation and colour settings of this panel as mounted; confirmed on the hardware. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, true), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");
    return ESP_OK;
}

static esp_err_t init_lvgl(void)
{
    /* The SPI driver transmits straight from this buffer, so it must be internal DMA memory. */
    uint8_t *draw_buf = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(draw_buf != NULL, ESP_ERR_NO_MEM, TAG, "draw buffer");

#if CONFIG_DEVOTCHI_SHADOW_FB
    s_shadow = heap_caps_calloc(1, FRAME_SIZE, MALLOC_CAP_SPIRAM);
    s_snapshot = heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_shadow != NULL && s_snapshot != NULL, ESP_ERR_NO_MEM, TAG, "framebuffers");
#endif

    lv_init();
    s_disp = lv_display_create(BOARD_DISP_H_RES, BOARD_DISP_V_RES);
    ESP_RETURN_ON_FALSE(s_disp != NULL, ESP_ERR_NO_MEM, TAG, "lv_display");
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    return ESP_OK;
}

esp_err_t display_init(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    s_trans_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lock != NULL && s_trans_done != NULL, ESP_ERR_NO_MEM, TAG, "semaphores");

    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(init_panel(), TAG, "panel");
    ESP_RETURN_ON_ERROR(init_lvgl(), TAG, "lvgl");
    ESP_LOGI(TAG, "ST7789 %dx%d, SPI %d Hz", BOARD_DISP_H_RES, BOARD_DISP_V_RES,
             CONFIG_DEVOTCHI_DISPLAY_SPI_HZ);
    return ESP_OK;
}

bool display_lock(uint32_t timeout_ms)
{
    return xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void display_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

void display_set_brightness(uint8_t percent)
{
    if (!display_lock(PANEL_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "brightness: lock timeout");
        return;
    }
    s_brightness = percent > 100 ? 100 : percent;
    apply_backlight();
    display_unlock();
}

esp_err_t display_sleep(bool sleep)
{
    ESP_RETURN_ON_FALSE(display_lock(PANEL_LOCK_TIMEOUT_MS), ESP_ERR_TIMEOUT, TAG, "sleep: lock timeout");
    esp_err_t err = ESP_OK;
    if (sleep != s_asleep) {
        if (sleep) {
            set_backlight(0);
        }
        /* The ST7789 driver already waits the 100 ms the controller needs after SLPIN/SLPOUT. */
        err = esp_lcd_panel_disp_sleep(s_panel, sleep);
        if (err == ESP_OK) {
            s_asleep = sleep;
        } else {
            ESP_LOGE(TAG, "panel sleep(%d) failed: %s", sleep, esp_err_to_name(err));
        }
        if (!sleep) {
            /*
             * The ui task draws the frame and flush_cb turns the backlight on
             * behind it. Drawing here would run LVGL's renderer, which needs
             * about 5 KB, on the caller's stack.
             */
            s_frame_shown = false;
            lv_obj_invalidate(lv_display_get_screen_active(s_disp));
        }
        apply_backlight();
    }
    display_unlock();
    return err;
}

uint8_t display_backlight_pct(void)
{
    const uint32_t duty = ledc_get_duty(BOARD_LEDC_MODE, BOARD_LEDC_CH_BACKLIGHT);
    return (uint8_t)((duty * 100U + BOARD_LEDC_DUTY_MAX / 2U) / BOARD_LEDC_DUTY_MAX);
}

bool display_is_asleep(void)
{
    bool asleep = false;
    if (display_lock(PANEL_LOCK_TIMEOUT_MS)) {
        asleep = s_asleep;
        display_unlock();
    }
    return asleep;
}

esp_err_t display_snapshot(const uint8_t **buf, size_t *len, uint32_t *crc32)
{
#if CONFIG_DEVOTCHI_SHADOW_FB
    /*
     * Pending changes are left to the ui task: rendering them here would run
     * LVGL's renderer on the console stack. LVGL pauses the refresh timer
     * while nothing is invalidated. A parked ui task never catches up, and
     * the copy is then the frame the panel last received.
     */
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        ESP_RETURN_ON_FALSE(display_lock(SNAPSHOT_LOCK_TIMEOUT_MS), ESP_ERR_TIMEOUT, TAG,
                            "snapshot: lock timeout");
        if (lv_timer_get_paused(lv_display_get_refr_timer(s_disp)) ||
            xTaskGetTickCount() - start >= pdMS_TO_TICKS(SNAPSHOT_SETTLE_MS)) {
            break;
        }
        display_unlock();
        vTaskDelay(pdMS_TO_TICKS(SNAPSHOT_POLL_MS));
    }
    memcpy(s_snapshot, s_shadow, FRAME_SIZE);
    display_unlock();

    *buf = s_snapshot;
    *len = FRAME_SIZE;
    *crc32 = esp_rom_crc32_le(0, s_snapshot, FRAME_SIZE);
    return ESP_OK;
#else
    (void)buf;
    (void)len;
    (void)crc32;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

uint32_t display_flush_errors(void)
{
    return atomic_load(&s_flush_errors);
}

uint32_t display_flush_count(void)
{
    return atomic_load(&s_flush_count);
}
