#include "screens/PairingScreen.hpp"

#include "Config.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include <cstring>

namespace devotchi::ui {

namespace {
constexpr const char* TAG = "PairingScreen";

int alpha_index_of(char c) {
    auto* a = config::pairing::ALPHABET;
    for (int i = 0; a[i]; ++i) if (a[i] == c) return i;
    return 0;
}

char alpha_at(int i) {
    auto* a = config::pairing::ALPHABET;
    int   n = static_cast<int>(std::strlen(a));
    if (i < 0) i += n * (1 + (-i / n));
    return a[i % n];
}
}  // namespace

PairingScreen::PairingScreen(SubmitCallback on_submit) : submit_cb_(std::move(on_submit)) {
    for (int i = 0; i < 6; ++i) code_[i] = config::pairing::ALPHABET[0];
    code_[6] = '\0';
}

void PairingScreen::enter() {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "Enter pairing code");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    constexpr int char_w = 32;
    constexpr int gap    = 8;
    constexpr int total_w = 6 * char_w + 5 * gap;
    const int x0 = (320 - total_w) / 2;

    for (int i = 0; i < 6; ++i) {
        auto* lbl = lv_label_create(root_);
        char buf[2] = { code_[i], '\0' };
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_pos(lbl, x0 + i * (char_w + gap), 100);
        labels_[i] = lbl;
    }

    hint_ = lv_label_create(root_);
    lv_label_set_text(hint_, "<- prev | next ->\nACTION: advance / hold to submit");
    lv_obj_set_style_text_color(hint_, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -10);

    error_ = lv_label_create(root_);
    lv_label_set_text(error_, "");
    lv_obj_set_style_text_color(error_, lv_color_hex(0xFF4040), 0);
    lv_obj_align(error_, LV_ALIGN_CENTER, 0, 50);

    redraw_chars();
}

void PairingScreen::update() {
    if (error_until_ms_ != 0) {
        const uint32_t now = esp_timer_get_time() / 1000ULL;
        if (now > error_until_ms_) {
            lv_label_set_text(error_, "");
            error_until_ms_ = 0;
        }
    }
}

void PairingScreen::on_button(buttons::ButtonEvent ev) {
    using namespace buttons;
    if (ev.kind == EventKind::Press) {
        switch (ev.id) {
            case ButtonId::Approve:
                code_[pos_] = alpha_at(alpha_index_of(code_[pos_]) + 1);
                redraw_chars();
                break;
            case ButtonId::Reject:
                code_[pos_] = alpha_at(alpha_index_of(code_[pos_]) - 1);
                redraw_chars();
                break;
            case ButtonId::Action:
                if (pos_ < 5) { pos_++; redraw_chars(); }
                else if (submit_cb_) {
                    ESP_LOGI(TAG, "submitting code %s", code_);
                    submit_cb_(std::string(code_));
                }
                break;
        }
    } else if (ev.kind == EventKind::LongPress && ev.id == ButtonId::Action) {
        if (pos_ > 0) { pos_--; redraw_chars(); }
    }
}

void PairingScreen::show_error(const char* msg) {
    if (error_) {
        lv_label_set_text(error_, msg);
        error_until_ms_ = (esp_timer_get_time() / 1000ULL) + 2000;
    }
}

void PairingScreen::redraw_chars() {
    for (int i = 0; i < 6; ++i) {
        if (!labels_[i]) continue;
        char buf[2] = { code_[i], '\0' };
        lv_label_set_text(labels_[i], buf);
        const bool active = (i == pos_);
        lv_obj_set_style_text_color(labels_[i],
            active ? lv_color_hex(0xFFD040) : lv_color_white(), 0);
    }
}

}  // namespace devotchi::ui
