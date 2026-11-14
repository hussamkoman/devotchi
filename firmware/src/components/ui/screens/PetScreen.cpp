#include "screens/PetScreen.hpp"

#include "esp_log.h"
#include "esp_timer.h"

namespace devotchi::ui {

namespace {
constexpr const char* TAG = "PetScreen";

constexpr int FACE_CX_LEFT  = 110;
constexpr int FACE_CX_RIGHT = 210;
constexpr int FACE_CY_EYE   = 110;
constexpr int FACE_CY_MOUTH = 165;

const char* mood_to_label(protocol::Mood m) {
    using protocol::Mood;
    switch (m) {
        case Mood::Happy:    return "happy";
        case Mood::Sad:      return "sad";
        case Mood::Excited:  return "excited";
        case Mood::Anxious:  return "anxious";
        case Mood::Sleeping: return "sleeping";
        case Mood::Angry:    return "angry";
        case Mood::Proud:    return "proud";
        case Mood::Focused:  return "focused";
        case Mood::Working:  return "working";
        case Mood::Thinking: return "thinking";
        case Mood::Idle:     return "idle";
    }
    return "idle";
}

lv_obj_t* make_circle(lv_obj_t* parent, int cx, int cy, int r, lv_color_t color) {
    auto* o = lv_obj_create(parent);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_pos(o, cx - r, cy - r);
    return o;
}

lv_obj_t* make_rect(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color) {
    auto* o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_pos(o, x, y);
    return o;
}

void draw_eye_round(lv_obj_t* parent, int cx, int cy, int r) {
    make_circle(parent, cx, cy, r, lv_color_white());
}

void draw_eye_slit(lv_obj_t* parent, int cx, int cy) {
    make_rect(parent, cx - 14, cy - 2, 28, 4, lv_color_white());
}

void draw_eye_caret_up(lv_obj_t* parent, int cx, int cy) {
    // ^ shape — two short diagonal rects approximated as horizontal blocks
    make_rect(parent, cx - 12, cy + 4, 12, 4, lv_color_white());
    make_rect(parent, cx,      cy + 4, 12, 4, lv_color_white());
    make_rect(parent, cx - 6,  cy,     12, 4, lv_color_white());
}

void draw_eye_angry(lv_obj_t* parent, int cx, int cy) {
    // small round eye + brow above
    make_circle(parent, cx, cy + 4, 8, lv_color_white());
    make_rect(parent, cx - 14, cy - 8, 28, 4, lv_color_hex(0xFF6060));
}

void draw_mouth_line(lv_obj_t* parent, int cx, int cy) {
    make_rect(parent, cx - 40, cy, 80, 4, lv_color_white());
}

void draw_mouth_smile(lv_obj_t* parent, int cx, int cy) {
    make_rect(parent, cx - 30, cy + 6, 60, 4, lv_color_white());
    make_rect(parent, cx - 36, cy + 2, 8,  4, lv_color_white());
    make_rect(parent, cx + 28, cy + 2, 8,  4, lv_color_white());
}

void draw_mouth_frown(lv_obj_t* parent, int cx, int cy) {
    make_rect(parent, cx - 30, cy,     60, 4, lv_color_white());
    make_rect(parent, cx - 36, cy + 4, 8,  4, lv_color_white());
    make_rect(parent, cx + 28, cy + 4, 8,  4, lv_color_white());
}

void draw_mouth_zigzag(lv_obj_t* parent, int cx, int cy) {
    for (int i = -3; i <= 3; ++i) {
        const int dy = (i & 1) ? 6 : 0;
        make_rect(parent, cx + i * 10, cy + dy, 10, 3, lv_color_white());
    }
}

void draw_face_for_mood(lv_obj_t* parent, protocol::Mood m) {
    using protocol::Mood;
    switch (m) {
        case Mood::Happy:
            draw_eye_round(parent, FACE_CX_LEFT,  FACE_CY_EYE, 14);
            draw_eye_round(parent, FACE_CX_RIGHT, FACE_CY_EYE, 14);
            draw_mouth_smile(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Sad:
            draw_eye_round(parent, FACE_CX_LEFT,  FACE_CY_EYE, 12);
            draw_eye_round(parent, FACE_CX_RIGHT, FACE_CY_EYE, 12);
            draw_mouth_frown(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Excited:
            draw_eye_round(parent, FACE_CX_LEFT,  FACE_CY_EYE, 18);
            draw_eye_round(parent, FACE_CX_RIGHT, FACE_CY_EYE, 18);
            make_circle(parent, FACE_CX_LEFT - 18,  FACE_CY_EYE - 14, 3, lv_color_hex(0xFFD040));
            make_circle(parent, FACE_CX_RIGHT + 18, FACE_CY_EYE - 14, 3, lv_color_hex(0xFFD040));
            draw_mouth_smile(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Anxious:
            draw_eye_round(parent, FACE_CX_LEFT,  FACE_CY_EYE, 10);
            draw_eye_round(parent, FACE_CX_RIGHT, FACE_CY_EYE, 10);
            draw_mouth_zigzag(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Sleeping:
            draw_eye_slit(parent, FACE_CX_LEFT,  FACE_CY_EYE);
            draw_eye_slit(parent, FACE_CX_RIGHT, FACE_CY_EYE);
            make_rect(parent, 150, FACE_CY_MOUTH, 20, 4, lv_color_white());
            break;
        case Mood::Angry:
            draw_eye_angry(parent, FACE_CX_LEFT,  FACE_CY_EYE);
            draw_eye_angry(parent, FACE_CX_RIGHT, FACE_CY_EYE);
            draw_mouth_frown(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Proud:
            draw_eye_caret_up(parent, FACE_CX_LEFT,  FACE_CY_EYE);
            draw_eye_caret_up(parent, FACE_CX_RIGHT, FACE_CY_EYE);
            draw_mouth_smile(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Focused:
            draw_eye_slit(parent, FACE_CX_LEFT,  FACE_CY_EYE);
            draw_eye_slit(parent, FACE_CX_RIGHT, FACE_CY_EYE);
            draw_mouth_line(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Working:
            draw_eye_slit(parent, FACE_CX_LEFT,  FACE_CY_EYE);
            draw_eye_slit(parent, FACE_CX_RIGHT, FACE_CY_EYE);
            draw_mouth_line(parent, 160, FACE_CY_MOUTH);
            // dot dot dot suggestion
            make_circle(parent, 230, FACE_CY_MOUTH + 2, 2, lv_color_white());
            make_circle(parent, 240, FACE_CY_MOUTH + 2, 2, lv_color_white());
            make_circle(parent, 250, FACE_CY_MOUTH + 2, 2, lv_color_white());
            break;
        case Mood::Thinking:
            draw_eye_round(parent, FACE_CX_LEFT,  FACE_CY_EYE - 4, 12);
            draw_eye_round(parent, FACE_CX_RIGHT, FACE_CY_EYE - 4, 12);
            draw_mouth_line(parent, 160, FACE_CY_MOUTH);
            break;
        case Mood::Idle:
        default:
            draw_eye_round(parent, FACE_CX_LEFT,  FACE_CY_EYE, 14);
            draw_eye_round(parent, FACE_CX_RIGHT, FACE_CY_EYE, 14);
            draw_mouth_line(parent, 160, FACE_CY_MOUTH);
            break;
    }
}
}  // namespace

void PetScreen::enter() {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);

    face_root_ = lv_obj_create(root_);
    lv_obj_set_size(face_root_, 320, 200);
    lv_obj_set_pos(face_root_, 0, 20);
    lv_obj_set_style_bg_opa(face_root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_root_, 0, 0);
    lv_obj_set_style_pad_all(face_root_, 0, 0);

    status_label_ = lv_label_create(root_);
    lv_label_set_text(status_label_, "");
    lv_obj_set_width(status_label_, 280);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status_label_, LV_ALIGN_BOTTOM_MID, 0, -22);

    mood_label_ = lv_label_create(root_);
    lv_label_set_text(mood_label_, mood_to_label(mood_));
    lv_obj_set_style_text_color(mood_label_, lv_color_white(), 0);
    lv_obj_align(mood_label_, LV_ALIGN_BOTTOM_MID, 0, -4);

    rebuild_face();

    ESP_LOGI(TAG, "PetScreen entered");
}

void PetScreen::update() {
    if (toast_ && toast_until_ms_ != 0) {
        const uint64_t now = esp_timer_get_time() / 1000ULL;
        if (now > toast_until_ms_) {
            lv_obj_delete(toast_);
            toast_ = nullptr;
            toast_until_ms_ = 0;
        }
    }
}

void PetScreen::on_button(buttons::ButtonEvent ev) {
    ESP_LOGI(TAG, "button: id=%d kind=%d",
             static_cast<int>(ev.id), static_cast<int>(ev.kind));
}

void PetScreen::on_message(const protocol::Message& msg) {
    if (auto* m = std::get_if<protocol::MoodMessage>(&msg)) {
        mood_ = m->mood;
        rebuild_face();
        if (mood_label_) lv_label_set_text(mood_label_, mood_to_label(mood_));
    } else if (auto* s = std::get_if<protocol::StatusMessage>(&msg)) {
        mood_ = s->mood;
        status_text_ = s->msg;
        rebuild_face();
        if (status_label_) lv_label_set_text(status_label_, status_text_.c_str());
        if (mood_label_)   lv_label_set_text(mood_label_,   mood_to_label(mood_));
    } else if (auto* n = std::get_if<protocol::NotifyMessage>(&msg)) {
        mood_ = n->mood;
        rebuild_face();
        if (mood_label_) lv_label_set_text(mood_label_, mood_to_label(mood_));
        std::string text = n->event.empty() ? n->detail
                                             : (n->event + (n->detail.empty() ? "" : ": " + n->detail));
        if (!text.empty()) show_toast(text);
    }
}

void PetScreen::rebuild_face() {
    if (!face_root_) return;
    lv_obj_clean(face_root_);
    draw_face_for_mood(face_root_, mood_);
}

void PetScreen::show_toast(const std::string& text) {
    if (toast_) {
        lv_obj_delete(toast_);
        toast_ = nullptr;
    }
    toast_ = lv_obj_create(root_);
    lv_obj_set_size(toast_, 280, 36);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(toast_, lv_color_hex(0x202040), 0);
    lv_obj_set_style_border_color(toast_, lv_color_hex(0xFFD040), 0);
    lv_obj_set_style_border_width(toast_, 1, 0);
    lv_obj_set_style_radius(toast_, 6, 0);
    lv_obj_set_style_pad_all(toast_, 4, 0);

    auto* lbl = lv_label_create(toast_);
    lv_label_set_text(lbl, text.c_str());
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, 270);
    lv_obj_center(lbl);

    toast_until_ms_ = (esp_timer_get_time() / 1000ULL) + 2500;
}

}  // namespace devotchi::ui
