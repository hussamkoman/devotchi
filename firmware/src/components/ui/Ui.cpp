#include "Ui.hpp"

#include "Config.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by queue.h / semphr.h
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace devotchi::ui {

namespace {
constexpr const char* TAG = "Ui";
constexpr UBaseType_t BUTTON_QUEUE_DEPTH = 8;
constexpr int         DRAW_BUF_LINES     = 40;   // 320 * 40 * 2 = 25 KB per buffer
}  // namespace

void Ui::init(display::Display& display) {
    display_ = &display;

    swap_mutex_ = xSemaphoreCreateMutex();
    configASSERT(swap_mutex_ != nullptr);

    button_q_ = xQueueCreate(BUTTON_QUEUE_DEPTH, sizeof(buttons::ButtonEvent));
    configASSERT(button_q_ != nullptr);

    msg_q_ = xQueueCreate(8, sizeof(protocol::Message*));
    configASSERT(msg_q_ != nullptr);

    lv_init();

    const size_t buf_bytes = config::display::WIDTH * DRAW_BUF_LINES * sizeof(uint16_t);
    draw_buf1_ = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    draw_buf2_ = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!draw_buf1_ || !draw_buf2_) {
        ESP_LOGE(TAG, "draw buffer allocation failed (%u bytes each)", (unsigned)buf_bytes);
        return;
    }

    lv_disp_ = lv_display_create(config::display::WIDTH, config::display::HEIGHT);
    lv_display_set_user_data(lv_disp_, this);
    lv_display_set_flush_cb(lv_disp_, &Ui::flush_cb);
    lv_display_set_buffers(lv_disp_, draw_buf1_, draw_buf2_, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    last_tick_ms_ = esp_timer_get_time() / 1000ULL;

    status_bar_.init();
    status_bar_.show(false);   // hidden by default; App reveals it on PetScreen

    ESP_LOGI(TAG, "init — LVGL %dx%d, draw buffers %u B x2 in PSRAM",
             config::display::WIDTH, config::display::HEIGHT, (unsigned)buf_bytes);
}

void Ui::show(std::unique_ptr<Screen> screen) {
    auto m = static_cast<SemaphoreHandle_t>(swap_mutex_);
    xSemaphoreTake(m, portMAX_DELAY);
    next_ = std::move(screen);
    xSemaphoreGive(m);
}

void Ui::on_button(buttons::ButtonEvent ev) {
    auto q = static_cast<QueueHandle_t>(button_q_);
    xQueueSend(q, &ev, 0);
}

void Ui::on_message(const protocol::Message& msg) {
    auto* heap_msg = new protocol::Message(msg);
    auto q = static_cast<QueueHandle_t>(msg_q_);
    if (xQueueSend(q, &heap_msg, 0) != pdTRUE) {
        delete heap_msg;
    }
}

void Ui::tick() {
    if (!lv_disp_) return;

    const uint32_t now_ms = esp_timer_get_time() / 1000ULL;
    lv_tick_inc(now_ms - last_tick_ms_);
    last_tick_ms_ = now_ms;

    auto q = static_cast<QueueHandle_t>(button_q_);
    buttons::ButtonEvent ev;
    while (xQueueReceive(q, &ev, 0) == pdTRUE) {
        if (current_) current_->on_button(ev);
    }

    auto mq = static_cast<QueueHandle_t>(msg_q_);
    protocol::Message* incoming_msg = nullptr;
    while (xQueueReceive(mq, &incoming_msg, 0) == pdTRUE) {
        if (current_ && incoming_msg) current_->on_message(*incoming_msg);
        delete incoming_msg;
    }

    auto m = static_cast<SemaphoreHandle_t>(swap_mutex_);
    xSemaphoreTake(m, portMAX_DELAY);
    std::unique_ptr<Screen> incoming = std::move(next_);
    xSemaphoreGive(m);

    if (incoming) {
        incoming->enter();
        if (incoming->root()) {
            lv_screen_load(incoming->root());
        }
        current_ = std::move(incoming);   // old screen's dtor runs here, deletes its lv_obj_t
    }

    // Apply staged status-bar state — only place LVGL touches these widgets.
    const bool want_visible = status_bar_visible_.load(std::memory_order_relaxed);
    if (want_visible != last_visible_) {
        status_bar_.show(want_visible);
        last_visible_ = want_visible;
    }
    if (want_visible) {
        const uint32_t packed = status_packed_.load(std::memory_order_relaxed);
        const uint8_t batt     = static_cast<uint8_t>(packed & 0xFF);
        const bool    charging = (packed >> 8)  & 1;
        const bool    wifi     = (packed >> 9)  & 1;
        const bool    paired   = (packed >> 10) & 1;
        status_bar_.update(batt, charging, wifi, paired);
    }

    if (current_) current_->update();

    lv_timer_handler();
}

void Ui::flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    auto* self = static_cast<Ui*>(lv_display_get_user_data(disp));
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    self->display_->draw_bitmap(area->x1, area->y1, w, h,
                                reinterpret_cast<const uint16_t*>(px_map));
    lv_display_flush_ready(disp);
}

void Ui::set_status(uint8_t battery_pct, bool charging, bool wifi_connected, bool paired) {
    const uint32_t packed = static_cast<uint32_t>(battery_pct)
                          | (static_cast<uint32_t>(charging)       << 8)
                          | (static_cast<uint32_t>(wifi_connected) << 9)
                          | (static_cast<uint32_t>(paired)         << 10);
    status_packed_.store(packed, std::memory_order_relaxed);
}

void Ui::set_status_bar_visible(bool visible) {
    status_bar_visible_.store(visible, std::memory_order_relaxed);
}

}  // namespace devotchi::ui
