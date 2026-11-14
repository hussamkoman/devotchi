#include "ui_priv.h"

#include "ui_theme.h"

#define FACE_W        200
#define FACE_H        130
#define EYE_Y         44
#define EYE_LEFT_X    62
#define EYE_RIGHT_X   138
#define MOUTH_POINTS  5
#define MOUTH_WIDTH   5

typedef struct {
    uint8_t eye_w;
    uint8_t eye_h;
    int8_t left_dy;
    int8_t right_dy;
    const lv_point_precise_t *mouth;
} face_t;

static const lv_point_precise_t SMILE[MOUTH_POINTS] = {{70, 92}, {85, 104}, {100, 108}, {115, 104}, {130, 92}};
static const lv_point_precise_t GRIN[MOUTH_POINTS] = {{62, 88}, {80, 108}, {100, 114}, {120, 108}, {138, 88}};
static const lv_point_precise_t FROWN[MOUTH_POINTS] = {{72, 110}, {86, 100}, {100, 97}, {114, 100}, {128, 110}};
static const lv_point_precise_t FLAT[MOUTH_POINTS] = {{78, 100}, {90, 100}, {100, 100}, {110, 100}, {122, 100}};
static const lv_point_precise_t SMALL[MOUTH_POINTS] = {{90, 100}, {95, 100}, {100, 100}, {105, 100}, {110, 100}};
static const lv_point_precise_t WAVY[MOUTH_POINTS] = {{72, 102}, {86, 95}, {100, 104}, {114, 95}, {128, 102}};
static const lv_point_precise_t GRIT[MOUTH_POINTS] = {{76, 108}, {88, 101}, {100, 100}, {112, 101}, {124, 108}};
static const lv_point_precise_t SMIRK[MOUTH_POINTS] = {{74, 98}, {90, 103}, {104, 103}, {118, 98}, {130, 88}};
static const lv_point_precise_t HMM[MOUTH_POINTS] = {{96, 103}, {104, 102}, {112, 101}, {120, 100}, {128, 98}};
static const lv_point_precise_t BUSY[MOUTH_POINTS] = {{84, 100}, {92, 104}, {100, 105}, {108, 104}, {116, 100}};

static const face_t FACES[MOOD_COUNT] = {
    [MOOD_HAPPY] = {26, 26, 0, 0, SMILE},
    [MOOD_SAD] = {22, 16, 6, 6, FROWN},
    [MOOD_EXCITED] = {32, 32, -2, -2, GRIN},
    [MOOD_ANXIOUS] = {18, 30, 0, 0, WAVY},
    [MOOD_SLEEPING] = {28, 4, 8, 8, SMALL},
    [MOOD_ANGRY] = {28, 10, 2, 2, GRIT},
    [MOOD_PROUD] = {26, 12, 0, 0, SMIRK},
    [MOOD_IDLE] = {22, 22, 0, 0, FLAT},
    [MOOD_FOCUSED] = {24, 10, 0, 0, SMALL},
    [MOOD_WORKING] = {22, 18, 0, 0, BUSY},
    [MOOD_THINKING] = {20, 20, -6, 2, HMM},
};

static lv_obj_t *s_eye[2];
static lv_obj_t *s_mouth;
static bool s_has_mood;
static proto_mood_t s_mood;

static lv_obj_t *eye(lv_obj_t *face)
{
    lv_obj_t *o = lv_obj_create(face);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void pet_view_create(lv_obj_t *parent)
{
    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, FACE_W, FACE_H);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_eye[0] = eye(face);
    s_eye[1] = eye(face);
    s_mouth = lv_line_create(face);
    lv_obj_set_style_line_width(s_mouth, MOUTH_WIDTH, 0);
    lv_obj_set_style_line_rounded(s_mouth, true, 0);
    lv_obj_set_style_line_color(s_mouth, UI_COLOR_TEXT, 0);
    s_has_mood = false;
    pet_view_set_mood(MOOD_IDLE);
}

void pet_view_set_mood(proto_mood_t mood)
{
    if (mood >= MOOD_COUNT) {
        mood = MOOD_IDLE;
    }
    if (s_has_mood && mood == s_mood) {
        return;
    }
    s_has_mood = true;
    s_mood = mood;
    const face_t *f = &FACES[mood];
    const int32_t xs[2] = {EYE_LEFT_X, EYE_RIGHT_X};
    const int32_t dys[2] = {f->left_dy, f->right_dy};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(s_eye[i], f->eye_w, f->eye_h);
        lv_obj_set_pos(s_eye[i], xs[i] - f->eye_w / 2, EYE_Y + dys[i] - f->eye_h / 2);
    }
    lv_line_set_points(s_mouth, f->mouth, MOUTH_POINTS);
}
