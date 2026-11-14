#include "ui_priv.h"

#include "pet_pose.h"
#include "ui_theme.h"

#include "esp_random.h"

#include <stdlib.h>
#include <string.h>

/*
 * The group holds body and accessory and stays put. The body bobs and jitters
 * alone, with its features as children, which keeps the area redrawn per
 * frame small enough for the frame budget at 160 MHz.
 */
#define GROUP_W          190
#define GROUP_H          160
#define GROUP_Y          32
#define BODY_X           20
#define BODY_Y           24
#define BODY_W           150
#define BODY_H           124
#define BODY_RADIUS      46
/* Feature positions in body coordinates. */
#define EYE_Y            50
#define EYE_LEFT_X       48
#define EYE_RIGHT_X      102
#define EYE_MIN_H        4
/* Below this openness the eye is drawn as a closed lid. */
#define EYE_SHUT_OPEN    15
#define BROW_GAP         7
#define BROW_HALF        11
#define BROW_W           4
#define BROW_MIN_ANGLE   4
#define MOUTH_X          75
#define MOUTH_Y          90
#define MOUTH_HALF_W     24
#define MOUTH_SAG_MAX    14
#define MOUTH_W          4
#define MOUTH_POINTS     7
#define MOUTH_SHOW_OPEN  5
#define HOLE_MIN_W       24
#define HOLE_MIN_H       6
#define HOLE_MAX_H       26
#define ARC_POINTS       12
#define ARC_W            4
#define SQUISH_PX        12
#define POKE_EYE_OPEN    25

#define FRAME_MS         33
#define JITTER_STEP_MS   100
#define PAT_MS           1500
#define POKE_MS          600
#define DIZZY_MS         2000

typedef struct {
    int32_t x, y, w, h;
    bool valid;
} place_t;

static lv_obj_t *s_group;
static lv_obj_t *s_body;
static lv_obj_t *s_eye[2];
static lv_obj_t *s_pupil[2];
static lv_obj_t *s_brow[2];
static lv_obj_t *s_hole;
static lv_obj_t *s_mouth;
static lv_obj_t *s_arc[2];
static lv_obj_t *s_acc;
static lv_timer_t *s_timer;

/* Everything below is guarded by the ui lock. lv_line keeps a pointer to its points. */
static lv_point_precise_t s_brow_pts[2][2];
static lv_point_precise_t s_mouth_pts[MOUTH_POINTS];
static lv_point_precise_t s_arc_pts[2][ARC_POINTS];

static place_t s_place_group, s_place_body, s_place_eye[2], s_place_pupil[2], s_place_hole, s_place_acc;
static int32_t s_brow_key[2] = {INT32_MIN, INT32_MIN};
static int32_t s_mouth_key = INT32_MIN;
static int32_t s_arc_key = INT32_MIN;
static uint32_t s_body_color = UINT32_MAX;
static int s_eye_shut = -1;
static int s_acc_shown = -1;

static bool s_has_mood;
static proto_mood_t s_mood;
static pet_pose_t s_from;
static pet_pose_t s_to;
static pet_pose_t s_cur;
static uint32_t s_tween_start;
static uint16_t s_tween_permille = 1000;

static uint32_t s_blink_at;
static uint32_t s_blink_start;
static bool s_blinking;
static pet_glance_t s_glance;
static int16_t s_jitter;
static uint32_t s_jitter_at;
static int16_t s_bob;

static ui_pet_reaction_t s_reaction;
static uint32_t s_reaction_until;

static bool s_visible;
static bool s_powered = true;
static bool s_frozen;
static ui_pet_state_t s_state;

static uint32_t rand_cb(void *ctx)
{
    (void)ctx;
    return esp_random();
}

static bool reached(uint32_t at, uint32_t now)
{
    return (int32_t)(now - at) >= 0;
}

static lv_obj_t *plain(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *line(lv_obj_t *parent, int32_t width)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, UI_COLOR_PET_INK, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

static void place(lv_obj_t *o, place_t *cache, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (cache->valid && cache->x == x && cache->y == y && cache->w == w && cache->h == h) {
        return;
    }
    *cache = (place_t){.x = x, .y = y, .w = w, .h = h, .valid = true};
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
}

static void show(lv_obj_t *o, bool visible)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == visible) {
        if (visible) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static int32_t sin_scaled(int32_t degrees, int32_t amplitude)
{
    return (lv_trigo_sin((int16_t)(degrees % 360)) * amplitude) >> LV_TRIGO_SHIFT;
}

static int16_t clamp100(int32_t v)
{
    return (int16_t)(v < -100 ? -100 : v > 100 ? 100 : v);
}

/* Drops sit on the body and follow its bob and jitter; the other accessories float above it. */
static void place_drop(pet_accessory_t a)
{
    if (a == PET_ACC_SWEAT) {
        place(s_acc, &s_place_acc, BODY_X + 126 + s_jitter, BODY_Y + 14 + s_bob, 10, 15);
    } else if (a == PET_ACC_TEAR) {
        place(s_acc, &s_place_acc, BODY_X + EYE_LEFT_X - 14 + s_jitter, BODY_Y + EYE_Y + 16 + s_bob, 9, 13);
    }
}

static void set_accessory(pet_accessory_t a)
{
    if (s_acc_shown == (int)a) {
        place_drop(a);
        return;
    }
    s_acc_shown = (int)a;
    s_place_acc.valid = false;
    lv_obj_set_style_bg_opa(s_acc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_acc, 0, 0);
    lv_obj_set_style_text_letter_space(s_acc, 0, 0);
    lv_obj_set_size(s_acc, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    switch (a) {
    case PET_ACC_NONE:
    case PET_ACC_COUNT:
        lv_label_set_text_static(s_acc, "");
        show(s_acc, false);
        return;
    case PET_ACC_ZZZ:
        lv_label_set_text_static(s_acc, "Zzz");
        lv_obj_set_style_text_font(s_acc, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(s_acc, UI_COLOR_PET_ZZZ, 0);
        lv_obj_set_pos(s_acc, 146, 0);
        break;
    case PET_ACC_DOTS:
        lv_label_set_text_static(s_acc, "...");
        lv_obj_set_style_text_font(s_acc, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(s_acc, UI_COLOR_PET_DOTS, 0);
        lv_obj_set_pos(s_acc, 148, 0);
        break;
    case PET_ACC_SPARKLES:
        /* One label draws both sparkles: the letter spacing puts them at the two upper corners. */
        lv_label_set_text_static(s_acc, "++");
        lv_obj_set_style_text_font(s_acc, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(s_acc, UI_COLOR_PET_SPARKLE, 0);
        lv_obj_set_style_text_letter_space(s_acc, 150, 0);
        lv_obj_set_pos(s_acc, 4, 0);
        break;
    case PET_ACC_SWEAT:
    case PET_ACC_TEAR:
        lv_label_set_text_static(s_acc, "");
        lv_obj_set_style_bg_color(s_acc, UI_COLOR_PET_DROP, 0);
        lv_obj_set_style_bg_opa(s_acc, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_acc, LV_RADIUS_CIRCLE, 0);
        place_drop(a);
        break;
    }
    show(s_acc, true);
}

static void set_brow(int i, int32_t cx, int32_t y, int32_t angle, int32_t dx)
{
    const int32_t key = (cx + dx) * 1000000 + y * 1000 + angle;
    if (key == s_brow_key[i]) {
        return;
    }
    s_brow_key[i] = key;
    const int32_t d = sin_scaled(angle + 360, BROW_HALF);
    /* The inner end is the one nearer the middle of the face. */
    const int32_t left_dy = i == 0 ? -d : d;
    s_brow_pts[i][0] = (lv_point_precise_t){cx + dx - BROW_HALF, y + left_dy};
    s_brow_pts[i][1] = (lv_point_precise_t){cx + dx + BROW_HALF, y - left_dy};
    lv_line_set_points(s_brow[i], s_brow_pts[i], 2);
}

static void set_mouth(int32_t curve, int32_t dx)
{
    const int32_t key = curve * 1000 + dx;
    if (key == s_mouth_key) {
        return;
    }
    s_mouth_key = key;
    const int32_t sag = curve * MOUTH_SAG_MAX / 100;
    const int32_t half = (MOUTH_POINTS - 1) / 2;
    const int32_t h2 = half * half;
    for (int32_t i = 0; i < MOUTH_POINTS; i++) {
        const int32_t u = i - half;
        /* Rounded to the nearest pixel: truncation leaves a notch in shallow curves. */
        const int32_t num = sag * (h2 - u * u);
        s_mouth_pts[i] = (lv_point_precise_t){
            MOUTH_X + dx - MOUTH_HALF_W + i * 2 * MOUTH_HALF_W / (MOUTH_POINTS - 1),
            MOUTH_Y + (num >= 0 ? num + h2 / 2 : num - h2 / 2) / h2,
        };
    }
    lv_line_set_points(s_mouth, s_mouth_pts, MOUTH_POINTS);
}

/* Happy "^" arcs for a pat, spirals when dizzy. */
static void set_arcs(ui_pet_reaction_t r, int32_t dx)
{
    const int32_t key = (int32_t)r * 1000 + dx;
    if (key == s_arc_key) {
        return;
    }
    s_arc_key = key;
    const int32_t xs[2] = {EYE_LEFT_X + dx, EYE_RIGHT_X + dx};
    for (int i = 0; i < 2; i++) {
        uint32_t n;
        if (r == UI_PET_REACT_DIZZY) {
            n = ARC_POINTS;
            for (int32_t k = 0; k < ARC_POINTS; k++) {
                const int32_t radius = 2 + k;
                /* Opposite turning directions on the two eyes. */
                const int32_t deg = (i == 0 ? 1 : -1) * k * 60 + 360 * 4;
                s_arc_pts[i][k] = (lv_point_precise_t){
                    xs[i] + sin_scaled(deg + 90, radius),
                    EYE_Y + sin_scaled(deg, radius),
                };
            }
        } else {
            n = 3;
            s_arc_pts[i][0] = (lv_point_precise_t){xs[i] - 11, EYE_Y + 5};
            s_arc_pts[i][1] = (lv_point_precise_t){xs[i], EYE_Y - 6};
            s_arc_pts[i][2] = (lv_point_precise_t){xs[i] + 11, EYE_Y + 5};
        }
        lv_line_set_points(s_arc[i], s_arc_pts[i], n);
    }
}

static void schedule_blink(uint32_t now)
{
    const uint32_t wait = pet_blink_next(&s_cur, esp_random());
    s_blink_at = wait > 0 ? now + wait : 0;
}

/* Brings every object in line with the pose at now. */
static void render(uint32_t now)
{
    if (s_tween_permille < 1000) {
        const uint32_t t = now - s_tween_start;
        s_tween_permille = s_frozen || t >= PET_TWEEN_MS ? 1000 : (uint16_t)(t * 1000 / PET_TWEEN_MS);
        pet_pose_lerp(&s_from, &s_to, s_tween_permille, &s_cur);
    }
    const pet_pose_t *p = &s_cur;

    if (s_reaction != UI_PET_REACT_NONE && reached(s_reaction_until, now)) {
        s_reaction = UI_PET_REACT_NONE;
    }

    if (s_frozen) {
        s_blinking = false;
    } else if (s_blinking && now - s_blink_start >= PET_BLINK_MS) {
        s_blinking = false;
        schedule_blink(now);
    } else if (!s_blinking && s_blink_at != 0 && reached(s_blink_at, now)) {
        s_blinking = true;
        s_blink_start = now;
    } else if (!s_blinking && s_blink_at == 0 && p->blink_min_ms > 0) {
        schedule_blink(now);
    }

    pet_glance_set_mode(&s_glance, s_frozen ? PET_GLANCE_OFF : (pet_glance_mode_t)p->glance, now);
    pet_glance_update(&s_glance, now);

    s_bob = 0;
    if (!s_frozen && p->bob_amp_px > 0 && p->bob_period_ms > 0) {
        const uint32_t period = (uint32_t)p->bob_period_ms;
        s_bob = (int16_t)sin_scaled((int32_t)((now % period) * 360 / period), p->bob_amp_px);
    }
    int32_t jitter_amp = p->jitter_px;
    if (s_reaction == UI_PET_REACT_DIZZY) {
        jitter_amp = 2;
    }
    if (s_frozen || jitter_amp == 0) {
        s_jitter = 0;
    } else if (reached(s_jitter_at, now)) {
        s_jitter_at = now + JITTER_STEP_MS;
        s_jitter = (int16_t)((int32_t)(esp_random() % (uint32_t)(2 * jitter_amp + 1)) - jitter_amp);
    }

    int32_t open = p->eye_open;
    if (s_blinking) {
        const int32_t half = PET_BLINK_MS / 2;
        const int32_t t = (int32_t)(now - s_blink_start);
        open = open * abs(t - half) / half;
    }
    const bool squish = s_reaction == UI_PET_REACT_POKE;
    const bool arcs = s_reaction == UI_PET_REACT_PAT || s_reaction == UI_PET_REACT_DIZZY;
    if (squish && open > POKE_EYE_OPEN) {
        open = POKE_EYE_OPEN;
    }
    const int16_t pdx = clamp100(p->pupil_dx + s_glance.dx);
    const int16_t pdy = clamp100(p->pupil_dy + s_glance.dy);

    const int32_t sq = squish ? SQUISH_PX : 0;
    place(s_group, &s_place_group, (LV_HOR_RES - GROUP_W) / 2, GROUP_Y, GROUP_W, GROUP_H);
    place(s_body, &s_place_body, BODY_X - sq / 2 + s_jitter, BODY_Y + sq + s_bob, BODY_W + sq, BODY_H - sq);
    if (p->body_color != s_body_color) {
        s_body_color = p->body_color;
        lv_obj_set_style_bg_color(s_body, lv_color_hex(p->body_color), 0);
    }

    /* Features keep their place on the body when it widens. */
    const int32_t dx = sq / 2;
    const int32_t size = p->eye_size;
    const int32_t xs[2] = {EYE_LEFT_X + dx, EYE_RIGHT_X + dx};
    const bool shut = open < EYE_SHUT_OPEN;
    if (s_eye_shut != (int)shut) {
        s_eye_shut = (int)shut;
        for (int i = 0; i < 2; i++) {
            lv_obj_set_style_bg_color(s_eye[i], shut ? UI_COLOR_PET_INK : UI_COLOR_PET_EYE, 0);
            show(s_pupil[i], !shut);
        }
    }
    int32_t eye_h = size * open / 100;
    if (eye_h < EYE_MIN_H) {
        eye_h = EYE_MIN_H;
    }
    const int32_t pupil = size * p->pupil_size / 100;
    const int32_t travel = (size - pupil) / 2;
    for (int i = 0; i < 2; i++) {
        show(s_eye[i], !arcs);
        show(s_arc[i], arcs);
        place(s_eye[i], &s_place_eye[i], xs[i] - size / 2, EYE_Y - eye_h / 2, size, eye_h);
        place(s_pupil[i], &s_place_pupil[i], (size - pupil) / 2 + travel * pdx / 100,
              (eye_h - pupil) / 2 + travel * pdy / 100, pupil, pupil);
        const bool brows = abs(p->brow_angle) >= BROW_MIN_ANGLE;
        show(s_brow[i], brows);
        if (brows) {
            set_brow(i, xs[i] - dx, EYE_Y - size / 2 - BROW_GAP, p->brow_angle, dx);
        }
    }
    if (arcs) {
        set_arcs(s_reaction, dx);
    }

    int32_t curve = p->mouth_curve;
    int32_t mouth_open = p->mouth_open;
    if (s_reaction == UI_PET_REACT_PAT) {
        curve = 100;
    } else if (squish) {
        curve = 0;
        mouth_open = 30;
    } else if (s_reaction == UI_PET_REACT_DIZZY) {
        curve = -40;
    }
    const bool hole = mouth_open > MOUTH_SHOW_OPEN;
    show(s_hole, hole);
    show(s_mouth, !hole);
    if (hole) {
        int32_t h = mouth_open * HOLE_MAX_H / 100;
        if (h < HOLE_MIN_H) {
            h = HOLE_MIN_H;
        }
        const int32_t w = HOLE_MIN_W + mouth_open / 5;
        place(s_hole, &s_place_hole, MOUTH_X + dx - w / 2, MOUTH_Y - 2, w, h);
    } else {
        set_mouth(curve, dx);
    }
    set_accessory(s_reaction == UI_PET_REACT_NONE ? (pet_accessory_t)p->accessory : PET_ACC_NONE);

    s_state = (ui_pet_state_t){
        .mood = s_mood,
        .pose = s_cur,
        .tween_permille = s_tween_permille,
        .eye_open = (int16_t)open,
        .pupil_dx = pdx,
        .pupil_dy = pdy,
        .bob_px = s_bob,
        .jitter_px = s_jitter,
        .blinking = s_blinking,
        .reaction = s_reaction,
        .frozen = s_frozen,
        .animating = s_visible && s_powered,
    };
}

static void on_frame(lv_timer_t *t)
{
    (void)t;
    render(lv_tick_get());
}

static void update_timer(void)
{
    if (s_visible && s_powered) {
        const uint32_t now = lv_tick_get();
        /* Blink and glance restart from now rather than catching up on the time spent hidden. */
        schedule_blink(now);
        pet_glance_init(&s_glance, rand_cb, NULL, now);
        render(now);
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
        s_blinking = false;
        render(lv_tick_get());
    }
}

void pet_view_create(lv_obj_t *parent)
{
    s_group = lv_obj_create(parent);
    lv_obj_remove_style_all(s_group);
    lv_obj_remove_flag(s_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_group, UI_FLAG_NO_TEXT);

    s_body = plain(s_group, UI_COLOR_PET_EYE);
    lv_obj_set_style_radius(s_body, BODY_RADIUS, 0);
    for (int i = 0; i < 2; i++) {
        s_eye[i] = plain(s_body, UI_COLOR_PET_EYE);
        lv_obj_set_style_radius(s_eye[i], LV_RADIUS_CIRCLE, 0);
        s_pupil[i] = plain(s_eye[i], UI_COLOR_PET_INK);
        lv_obj_set_style_radius(s_pupil[i], LV_RADIUS_CIRCLE, 0);
        s_brow[i] = line(s_body, BROW_W);
        s_arc[i] = line(s_body, ARC_W);
        lv_obj_add_flag(s_arc[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_hole = plain(s_body, UI_COLOR_PET_INK);
    lv_obj_set_style_radius(s_hole, LV_RADIUS_CIRCLE, 0);
    s_mouth = line(s_body, MOUTH_W);
    s_acc = lv_label_create(s_group);
    lv_label_set_text_static(s_acc, "");
    lv_obj_add_flag(s_acc, LV_OBJ_FLAG_HIDDEN);

    const uint32_t now = lv_tick_get();
    pet_glance_init(&s_glance, rand_cb, NULL, now);
    s_timer = lv_timer_create(on_frame, FRAME_MS, NULL);
    lv_timer_pause(s_timer);
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
    const uint32_t now = lv_tick_get();
    s_to = *pet_pose_for_mood(mood);
    if (!s_has_mood || s_frozen) {
        s_from = s_to;
        s_cur = s_to;
        s_tween_permille = 1000;
    } else {
        s_from = s_cur;
        s_tween_permille = 0;
        s_tween_start = now;
    }
    s_has_mood = true;
    s_mood = mood;
    if (s_cur.blink_min_ms == 0 || s_to.blink_min_ms == 0) {
        s_blinking = false;
    }
    s_blink_at = 0;
    render(now);
}

void pet_view_set_visible(bool visible)
{
    show(s_group, visible);
    if (visible != s_visible) {
        s_visible = visible;
        update_timer();
    }
}

void pet_view_set_powered(bool powered)
{
    if (powered != s_powered) {
        s_powered = powered;
        update_timer();
    }
}

void pet_view_set_frozen(bool frozen)
{
    s_frozen = frozen;
    if (frozen) {
        s_tween_permille = 1000;
        s_cur = s_to;
    }
    render(lv_tick_get());
}

void pet_view_react(ui_pet_reaction_t r)
{
    const uint32_t now = lv_tick_get();
    s_reaction = r;
    switch (r) {
    case UI_PET_REACT_NONE:
        break;
    case UI_PET_REACT_PAT:
        s_reaction_until = now + PAT_MS;
        break;
    case UI_PET_REACT_POKE:
        s_reaction_until = now + POKE_MS;
        break;
    case UI_PET_REACT_DIZZY:
        s_reaction_until = now + DIZZY_MS;
        break;
    }
    render(now);
}

void pet_view_get_state(ui_pet_state_t *out)
{
    *out = s_state;
}
