#pragma once

#include "Buttons.hpp"
#include "Display.hpp"
#include "Protocol.hpp"
#include "Screen.hpp"
#include "Task.hpp"
#include "widgets/StatusBar.hpp"

#include "lvgl.h"

#include <atomic>
#include <memory>

namespace devotchi::ui {

/** Owns the LVGL display + draw buffers and dispatches to the active Screen.
 *  All LVGL calls run inside UiTask::tick(); external callers only stage state
 *  via atomics or queues. */
class Ui {
public:
    void init(display::Display& display);

    void show(std::unique_ptr<Screen> screen);
    void on_button(buttons::ButtonEvent ev);
    void on_message(const protocol::Message& msg);

    /** UiTask only — drives lv_tick_inc + lv_timer_handler. */
    void tick();

    /** Stage status bar contents. Safe to call from any task; applied on next tick. */
    void set_status(uint8_t battery_pct, bool charging, bool wifi_connected, bool paired);

    /** Stage status bar visibility. Safe from any task. */
    void set_status_bar_visible(bool visible);

private:
    static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

    display::Display* display_     = nullptr;
    lv_display_t*     lv_disp_     = nullptr;
    void*             draw_buf1_   = nullptr;
    void*             draw_buf2_   = nullptr;

    std::unique_ptr<Screen> current_;
    std::unique_ptr<Screen> next_;
    void*             swap_mutex_  = nullptr;   // SemaphoreHandle_t
    void*             button_q_    = nullptr;   // QueueHandle_t
    void*             msg_q_       = nullptr;   // QueueHandle_t of protocol::Message*

    StatusBar         status_bar_;

    // Atomics staged from any task; UiTask reads + applies in tick().
    std::atomic<uint32_t> status_packed_{0};       // pct(8) | charging(1) | wifi(1) | paired(1)
    std::atomic<bool>     status_bar_visible_{false};
    bool                  last_visible_ = false;

    uint32_t          last_tick_ms_ = 0;
};

class UiTask : public runtime::Task {
public:
    explicit UiTask(Ui& target);

protected:
    void run() override;

private:
    Ui& target_;
};

}  // namespace devotchi::ui
