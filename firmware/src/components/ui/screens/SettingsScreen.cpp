#include "screens/SettingsScreen.hpp"

#include <cstdio>

namespace devotchi::ui {

namespace {
constexpr uint8_t  BRIGHTNESS_STEPS[] = { 25, 50, 75, 100 };
constexpr uint32_t SLEEP_STEPS_MS[]   = { 15'000, 30'000, 60'000, 120'000 };

uint8_t next_brightness(uint8_t cur) {
    for (size_t i = 0; i < sizeof(BRIGHTNESS_STEPS); ++i) {
        if (BRIGHTNESS_STEPS[i] == cur) return BRIGHTNESS_STEPS[(i + 1) % 4];
    }
    return BRIGHTNESS_STEPS[0];
}

uint32_t next_sleep(uint32_t cur) {
    for (size_t i = 0; i < sizeof(SLEEP_STEPS_MS) / sizeof(uint32_t); ++i) {
        if (SLEEP_STEPS_MS[i] == cur) return SLEEP_STEPS_MS[(i + 1) % 4];
    }
    return SLEEP_STEPS_MS[1];
}
}  // namespace

SettingsScreen::SettingsScreen(uint8_t initial_brightness,
                               uint32_t initial_sleep_ms,
                               BrightnessCallback on_brightness,
                               SleepCallback on_sleep,
                               FactoryResetCallback on_reset,
                               ExitCallback on_exit)
    : brightness_(initial_brightness),
      sleep_ms_(initial_sleep_ms),
      on_brightness_(std::move(on_brightness)),
      on_sleep_(std::move(on_sleep)),
      on_reset_(std::move(on_reset)),
      on_exit_(std::move(on_exit)) {}

void SettingsScreen::enter() {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 12, LV_PART_MAIN);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFD040), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    for (int i = 0; i < 3; ++i) {
        auto* row = lv_label_create(root_);
        lv_obj_set_style_text_color(row, lv_color_white(), 0);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 20, 40 + i * 30);
        rows_[i] = row;
    }

    auto* hint = lv_label_create(root_);
    lv_label_set_text(hint, "REJECT/APPROVE: nav  ACTION: change  hold: exit");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    redraw();
}

void SettingsScreen::on_button(buttons::ButtonEvent ev) {
    using namespace buttons;
    if (ev.kind == EventKind::LongPress && ev.id == ButtonId::Action) {
        if (on_exit_) on_exit_();
        return;
    }
    if (ev.kind != EventKind::Press) return;

    switch (ev.id) {
        case ButtonId::Approve: cursor_ = (cursor_ + 1) % 3; reset_armed_ = false; break;
        case ButtonId::Reject:  cursor_ = (cursor_ + 2) % 3; reset_armed_ = false; break;
        case ButtonId::Action:  cycle_current(); break;
    }
    redraw();
}

void SettingsScreen::cycle_current() {
    switch (cursor_) {
        case 0:
            brightness_ = next_brightness(brightness_);
            if (on_brightness_) on_brightness_(brightness_);
            break;
        case 1:
            sleep_ms_ = next_sleep(sleep_ms_);
            if (on_sleep_) on_sleep_(sleep_ms_);
            break;
        case 2:
            if (reset_armed_) {
                if (on_reset_) on_reset_();
            } else {
                reset_armed_ = true;
            }
            break;
    }
}

void SettingsScreen::redraw() {
    char buf[64];

    std::snprintf(buf, sizeof(buf), "Brightness: %u%%", brightness_);
    lv_label_set_text(rows_[0], buf);

    std::snprintf(buf, sizeof(buf), "Sleep: %us", static_cast<unsigned>(sleep_ms_ / 1000));
    lv_label_set_text(rows_[1], buf);

    std::snprintf(buf, sizeof(buf), "Factory reset%s", reset_armed_ ? " (confirm: ACTION)" : "");
    lv_label_set_text(rows_[2], buf);

    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_text_color(rows_[i],
            i == cursor_ ? lv_color_hex(0xFFD040) : lv_color_white(), 0);
    }
}

}  // namespace devotchi::ui
