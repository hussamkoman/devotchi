#include "ui_priv.h"

#include "pet_builtin.h"
#include "pet_pack.h"
#include "pet_pose.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"
#include "esp_random.h"

#include <stdlib.h>
#include <string.h>

/*
 * The rig draws a pet as a list of simple items (filled or ringed rounded
 * rects, polylines, triangles) in the draw event of one object, back to
 * front. Every frame the list is rebuilt from the pack, the pose and the aux
 * channels; only items whose geometry or colour changed are invalidated, as
 * a few merged rectangles. One object per pet instead of one per part keeps
 * an Ember (about 60 shapes) inside the LVGL heap, keeps triangles in their
 * place in the back-to-front order, and lets outline mode restyle every
 * shape without new objects.
 *
 * Geometry is computed in sixteenths of a body unit ("q") and turned into
 * pixels once, as q * scale / 256 rounded half away from zero, so a part and
 * its mirror land on the same pixels.
 */
#define BOX_W            220
#define BOX_H            190
/* The body centre sits this far below the top of the box (the mockups' viewBox). */
#define BOX_CY           107
#define MAX_ITEMS        128
#define ITEM_PTS         12
#define MAX_DIRTY        4
#define DIRTY_JOIN_PX    8
#define FRAME_MS         33
/*
 * A pet that misses the frame budget (architecture 7.4: Ember's flapping
 * wings, about 35 ms at 30 fps) animates at 15 fps until frames are cheap again.
 */
#define FRAME_SLOW_MS    66
#define SLOW_ABOVE_X10   300
#define FAST_BELOW_X10   200
#define JITTER_STEP_MS   100
#define LIFT_STEP_MS     50
#define Q                16
#define EYE_SHUT_OPEN    15
#define BROW_MIN_ANGLE   4
/* Ring eyes: the ring is 1/RING_DIV of the eye size wide; the pupil has RING_PUPIL_PCT of a round eye's radius. */
#define RING_DIV         7
#define RING_PUPIL_PCT   60
#define MOUTH_POINTS     7
#define SPIRAL_POINTS    12
#define TEAR_FALL_MS     2500
#define DOTS_STEP_MS     400

typedef enum {
    IT_HIDDEN,
    IT_RECT,
    IT_LINE,
    IT_TRI,
} item_kind_t;

typedef struct {
    int16_t x, y;
} ipt_t;

typedef struct {
    uint8_t kind;
    uint8_t n;
    uint8_t width;
    /* rect: fill on/off and border width */
    uint8_t fill;
    uint8_t border;
    int8_t clip;
    int16_t radius;
    uint32_t color;
    uint32_t border_color;
    /* rect: x, y, w, h in p[0], p[1]; line and tri: the points. Pixels, relative to the view object. */
    ipt_t p[ITEM_PTS];
} item_t;

typedef struct {
    int32_t x1, y1, x2, y2;
    bool valid;
} box_t;

struct pet_view {
    lv_obj_t *obj;
    lv_timer_t *timer;
    pet_def_t def;
    uint8_t scale;
    int32_t cx, cy;
    int32_t ox, oy;

    proto_mood_t mood;
    bool has_mood;
    pet_pose_t from, to, cur;
    uint32_t col_from, col_to, col_cur;
    int16_t aux_from[PET_PACK_AUX_COUNT], aux_to[PET_PACK_AUX_COUNT];
    int16_t aux_cur[PET_PACK_AUX_COUNT], aux_sel[PET_PACK_AUX_COUNT];
    int16_t drop_from, drop_to, drop_cur;
    uint32_t tween_start;
    uint16_t tween;

    uint32_t blink_at, blink_start;
    bool blinking;
    pet_glance_t glance;
    int16_t jitter;
    uint32_t jitter_at;
    int16_t bob_q;
    int16_t bob_permille;
    int16_t osc_jitter[PET_PACK_MAX_OSC];
    uint32_t osc_at[PET_PACK_MAX_OSC];

    pet_react_t react;
    uint32_t react_start;
    pet_react_frame_t rf;

    int16_t lift_px, lift_target;
    uint32_t lift_at;

    bool visible, powered, frozen, outline, follow, slow;
    /* Frame period when frames are cheap (the clock face runs slower than home). */
    uint32_t frame_ms;

    uint16_t n_items;
    uint16_t dirty_count;
    item_t items[MAX_ITEMS];
    box_t prev_box[MAX_ITEMS];
    uint32_t prev_hash[MAX_ITEMS];
    /* Last point of each line part per side, in q, for anchored parts. */
    int32_t anchor_q[PET_PACK_MAX_PARTS][2][2];

    ui_pet_state_t state;
};

/* What a frozen reaction shows: the moment each one reads best. */
static const uint32_t FROZEN_AT[PET_REACT_COUNT] = {
    [PET_REACT_PAT] = 600,       [PET_REACT_POKE] = 100,      [PET_REACT_DIZZY] = 1000,
    [PET_REACT_WAKE] = 650,      [PET_REACT_ATTENTION] = 1000, [PET_REACT_POSITIVE] = 150,
    [PET_REACT_NEGATIVE] = 200,  [PET_REACT_COMPLETE] = 1000,
};

#define MAX_VIEWS  4

/* Guarded by the ui lock. */
static pet_view_t *s_views[MAX_VIEWS];
static pet_def_t s_def;
static bool s_def_ready;
static bool s_powered = true;

const pet_def_t *ui_pet_def(void)
{
    if (!s_def_ready) {
        const pet_builtin_t *b = pet_builtin_find(PET_BUILTIN_DEFAULT);
        s_def_ready = pet_pack_decode(b->bytes, b->len, &s_def) == PET_PACK_OK;
    }
    return &s_def;
}

static uint32_t rand_cb(void *ctx)
{
    (void)ctx;
    return esp_random();
}

static bool reached(uint32_t at, uint32_t now)
{
    return (int32_t)(now - at) >= 0;
}

static int32_t div_round(int32_t num, int32_t den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

/* q (sixteenths of a body unit) to pixels at the view's scale. */
static int32_t px(const pet_view_t *v, int32_t q)
{
    return div_round(q * v->scale, Q * 16);
}

static int32_t clampi(int32_t x, int32_t lo, int32_t hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

static uint32_t shade(uint32_t c, uint32_t to, unsigned t)
{
    return pet_pack_mix(c, to, t);
}

/* ---- item builders (q in, pixels out) ---- */

static item_t *slot(pet_view_t *v)
{
    if (v->n_items >= MAX_ITEMS) {
        return NULL;
    }
    item_t *it = &v->items[v->n_items++];
    memset(it, 0, sizeof(*it));
    it->clip = -1;
    return it;
}

/* Rect from centre and size in q; radius in q (clamped to half the short side). */
static void rect_q(pet_view_t *v, item_t *it, int32_t cx, int32_t cy, int32_t w, int32_t h, int32_t r, uint32_t color)
{
    if (it == NULL) {
        return;
    }
    const int32_t x1 = px(v, cx - w / 2);
    const int32_t y1 = px(v, cy - h / 2);
    int32_t pw = px(v, cx + w / 2) - x1;
    int32_t ph = px(v, cy + h / 2) - y1;
    if (pw < 1 || ph < 1) {
        it->kind = IT_HIDDEN;
        return;
    }
    int32_t pr = px(v, r);
    const int32_t half = (pw < ph ? pw : ph) / 2;
    if (pr > half) {
        pr = half;
    }
    it->kind = IT_RECT;
    it->fill = 1;
    it->p[0] = (ipt_t){(int16_t)(v->ox + x1), (int16_t)(v->oy + y1)};
    it->p[1] = (ipt_t){(int16_t)pw, (int16_t)ph};
    it->radius = (int16_t)pr;
    it->color = color;
}

static void circle_q(pet_view_t *v, item_t *it, int32_t cx, int32_t cy, int32_t r, uint32_t color)
{
    rect_q(v, it, cx, cy, 2 * r, 2 * r, r, color);
}

static void ring(pet_view_t *v, item_t *it, uint32_t color, int32_t width_q)
{
    if (it == NULL || it->kind != IT_RECT) {
        return;
    }
    it->fill = 0;
    it->border = (uint8_t)clampi(px(v, width_q), 2, 8);
    it->border_color = color;
}

static void line_q(pet_view_t *v, item_t *it, const int32_t (*pts)[2], int n, int32_t width_q, uint32_t color)
{
    if (it == NULL) {
        return;
    }
    it->kind = IT_LINE;
    it->n = (uint8_t)(n > ITEM_PTS ? ITEM_PTS : n);
    it->width = (uint8_t)clampi(px(v, width_q), 2, 16);
    it->color = color;
    for (int i = 0; i < it->n; i++) {
        it->p[i] = (ipt_t){(int16_t)(v->ox + px(v, pts[i][0])), (int16_t)(v->oy + px(v, pts[i][1]))};
    }
}

static void tri_q(pet_view_t *v, item_t *it, const int32_t (*pts)[2], uint32_t color)
{
    if (it == NULL) {
        return;
    }
    it->kind = IT_TRI;
    it->n = 3;
    it->color = color;
    for (int i = 0; i < 3; i++) {
        it->p[i] = (ipt_t){(int16_t)(v->ox + px(v, pts[i][0])), (int16_t)(v->oy + px(v, pts[i][1]))};
    }
}

/* Outline mode: fills become a 2 px border on black, lines 2 px, triangles closed polylines. */
static void to_outline(item_t *it)
{
    if (it == NULL || it->kind == IT_HIDDEN) {
        return;
    }
    if (it->kind == IT_RECT) {
        it->fill = 1;
        it->color = 0x000000;
        it->border = 2;
        it->border_color = UI_PET_NIGHT;
    } else if (it->kind == IT_LINE) {
        it->width = 2;
        it->color = UI_PET_NIGHT;
    } else {
        it->kind = IT_LINE;
        it->p[3] = it->p[0];
        it->n = 4;
        it->width = 2;
        it->color = UI_PET_NIGHT;
    }
}

static void hide(item_t *it)
{
    if (it != NULL) {
        it->kind = IT_HIDDEN;
    }
}

/* Quadratic Bezier p0 -> p2 with control c, n points, q in and out. */
static void quad(int32_t (*out)[2], int n, const int32_t p0[2], const int32_t c[2], const int32_t p2[2])
{
    for (int i = 0; i < n; i++) {
        const int32_t t = i * 1000 / (n - 1);
        const int32_t u = 1000 - t;
        for (int k = 0; k < 2; k++) {
            out[i][k] = (u * u / 1000 * p0[k] + 2 * u * t / 1000 * c[k] + t * t / 1000 * p2[k]) / 1000;
        }
    }
}

/* ---- colours ---- */

static uint32_t role_color(const pet_view_t *v, uint8_t role)
{
    const uint32_t body = pet_pack_body_color(&v->def, v->col_cur);
    switch (role) {
    case PET_PACK_ROLE_BODY:
        return body;
    case PET_PACK_ROLE_DARK:
        return shade(body, UI_PET_INK, UI_PET_DARK_T);
    case PET_PACK_ROLE_LIGHT:
        return shade(body, UI_PET_WHITE, UI_PET_LIGHT_T);
    case PET_PACK_ROLE_CHEEK:
        return shade(body, UI_PET_CORAL, UI_PET_CHEEK_T);
    case PET_PACK_ROLE_ACCENT:
        return v->def.accent;
    case PET_PACK_ROLE_ACCENT_DARK:
        return shade(v->def.accent, UI_PET_INK, UI_PET_ACC_DARK_T);
    case PET_PACK_ROLE_CREAM:
        return v->def.cream;
    case PET_PACK_ROLE_INK:
        return UI_PET_INK;
    case PET_PACK_ROLE_WHITE:
        return UI_PET_WHITE;
    case PET_PACK_ROLE_FLAME:
        return UI_PET_FLAME;
    case PET_PACK_ROLE_SURFACE:
        return UI_PET_SURFACE;
    case PET_PACK_ROLE_FOG:
        return UI_PET_FOG;
    case PET_PACK_ROLE_YELLOW:
        return UI_PET_YELLOW;
    case PET_PACK_ROLE_MOOD:
        return v->col_cur;
    default:
        return body;
    }
}

/* Colour of the face lines: pet ink, or the pack's cream on a body too dark for ink. */
static uint32_t face_ink(const pet_view_t *v)
{
    return pet_pack_dark(pet_pack_body_color(&v->def, v->col_cur)) ? v->def.cream : UI_PET_INK;
}

/* ---- frame state ---- */

typedef struct {
    pet_pose_t p;
    int32_t eye_open;
    int16_t pdx, pdy;
    /* Layer offsets in q. */
    int32_t sway;
    int32_t body_dy;
    int32_t head_dy;
    int32_t mouth_dy;
    int32_t hop;
    int32_t squish;
    int32_t lift;
} frame_t;

static void schedule_blink(pet_view_t *v, uint32_t now)
{
    const uint32_t wait = pet_blink_next(&v->cur, esp_random());
    v->blink_at = wait > 0 ? now + wait : 0;
}

static bool part_shown(const pet_view_t *v, const pet_part_t *part)
{
    for (uint8_t d = 0; d < part->n_drv; d++) {
        const pet_driver_t *drv = &part->drv[d];
        if (drv->op == PET_DRV_SHOW_NONZERO && v->aux_sel[drv->aux] == 0) {
            return false;
        }
        if (drv->op == PET_DRV_SHOW_EQ && v->aux_sel[drv->aux] != drv->arg) {
            return false;
        }
    }
    return true;
}

static void build_part(pet_view_t *v, const frame_t *f, int index, int side, item_t *it)
{
    const pet_part_t *part = &v->def.parts[index];
    const int32_t sx = side == 0 ? 1 : -1;
    int32_t pts[PET_PACK_MAX_POINTS][2];
    for (uint8_t k = 0; k < part->n_pts; k++) {
        pts[k][0] = part->pts[k].x * Q * sx;
        pts[k][1] = part->pts[k].y * Q;
    }
    for (uint8_t d = 0; d < part->n_drv; d++) {
        const pet_driver_t *drv = &part->drv[d];
        if (drv->op != PET_DRV_MOVE_X && drv->op != PET_DRV_MOVE_Y) {
            continue;
        }
        const int32_t amount = (int32_t)v->aux_cur[drv->aux] * drv->scale * (drv->op == PET_DRV_MOVE_X ? sx : 1);
        const int axis = drv->op == PET_DRV_MOVE_X ? 0 : 1;
        for (uint8_t k = 0; k < part->n_pts; k++) {
            if (drv->arg == PET_DRV_WHOLE || drv->arg == k) {
                pts[k][axis] += amount;
            }
        }
    }
    if (part->anchor >= 0) {
        const int a = part->anchor;
        const int as = v->def.parts[a].mirrored ? side : 0;
        const int32_t ax = v->def.parts[a].mirrored ? v->anchor_q[a][as][0] : v->anchor_q[a][0][0] * sx;
        for (uint8_t k = 0; k < part->n_pts; k++) {
            pts[k][0] += ax;
            pts[k][1] += v->anchor_q[a][as][1];
        }
    }
    if (part->shape == PET_SHAPE_LINE) {
        v->anchor_q[index][side][0] = pts[part->n_pts - 1][0];
        v->anchor_q[index][side][1] = pts[part->n_pts - 1][1];
    }
    int32_t dx = f->sway;
    int32_t dy = -f->lift;
    if (part->layer == PET_LAYER_BODY) {
        dy += f->body_dy;
    } else if (part->layer == PET_LAYER_HEAD || part->layer == PET_LAYER_FRONT) {
        dy += f->head_dy;
    } else {
        dx = 0;
    }
    for (uint8_t k = 0; k < part->n_pts; k++) {
        pts[k][0] += dx;
        pts[k][1] += dy;
    }
    if (!part_shown(v, part) || (v->outline ? part->no_outline : part->role == PET_PACK_ROLE_NIGHT)) {
        hide(it);
        return;
    }
    const uint32_t color = role_color(v, part->role);
    switch (part->shape) {
    case PET_SHAPE_RRECT: {
        int32_t w = part->w * Q;
        int32_t h = part->h * Q;
        int32_t cy = pts[0][1];
        if (part->squash) {
            h -= f->squish;
            cy += f->squish / 2;
        }
        if (part->layer == PET_LAYER_BEHIND && part->role == PET_PACK_ROLE_SURFACE) {
            w -= 4 * f->hop;
        }
        rect_q(v, it, pts[0][0], cy, w, h, part->r * Q, color);
        if (part->ring) {
            ring(v, it, color, 5 * Q / 2);
        }
        break;
    }
    case PET_SHAPE_LINE:
        line_q(v, it, (const int32_t(*)[2])pts, part->n_pts, part->width * Q, color);
        break;
    case PET_SHAPE_TRI:
        tri_q(v, it, (const int32_t(*)[2])pts, color);
        break;
    }
    if (v->outline) {
        to_outline(it);
    }
}

/* One eye: 7 items (sclera, pupil, glint, two lid triangles, lid edge, closed / arc / spiral line). */
static void build_eye(pet_view_t *v, const frame_t *f, int side, item_t **its)
{
    const pet_face_t *face = &v->def.face;
    const int32_t sx = side == 0 ? -1 : 1;
    const int32_t x = sx * face->eye_dx * Q + f->sway;
    const int32_t y = face->eye_y * Q + f->head_dy - f->lift;
    const int32_t w = f->p.eye_size * Q;
    const int32_t hf = w * 6 / 5;
    const uint32_t ink = v->outline ? UI_PET_NIGHT : face_ink(v);
    for (int i = 0; i < 7; i++) {
        hide(its[i]);
    }
    item_t *sclera = its[0], *pupil = its[1], *glint = its[2], *lid_a = its[3], *lid_b = its[4], *edge = its[5],
           *stroke = its[6];
    const int32_t sw = (v->outline ? 2 : 4) * Q;

    if (v->rf.active && v->rf.eyes == PET_EYES_ARC) {
        const int32_t p0[2] = {x - w / 2, y + 4 * Q}, c[2] = {x, y - 12 * Q}, p2[2] = {x + w / 2, y + 4 * Q};
        int32_t pts[5][2];
        quad(pts, 5, p0, c, p2);
        line_q(v, stroke, (const int32_t(*)[2])pts, 5, sw, ink);
        return;
    }
    if (v->rf.active && v->rf.eyes == PET_EYES_SPIRAL) {
        int32_t pts[SPIRAL_POINTS][2];
        const int32_t phase = (int32_t)v->rf.fx_permille * 360 / 1000 * (sx > 0 ? 1 : -1);
        for (int i = 0; i < SPIRAL_POINTS; i++) {
            const int32_t ang = i * 756 / (SPIRAL_POINTS - 1) + phase;
            const int32_t r = Q + i * (w / 2 - Q) / (SPIRAL_POINTS - 1);
            pts[i][0] = x + pet_sin(ang + 90, r);
            pts[i][1] = y + pet_sin(ang, r);
        }
        line_q(v, stroke, (const int32_t(*)[2])pts, SPIRAL_POINTS, 3 * Q, ink);
        return;
    }
    const int32_t eo = f->eye_open;
    if (eo < EYE_SHUT_OPEN) {
        const int32_t p0[2] = {x - w / 2, y}, c[2] = {x, y + 9 * Q}, p2[2] = {x + w / 2, y};
        int32_t pts[5][2];
        quad(pts, 5, p0, c, p2);
        line_q(v, stroke, (const int32_t(*)[2])pts, 5, sw, ink);
        return;
    }
    int32_t lid = f->p.lid;
    if (lid == PET_PACK_LID_OFF && !face->brows && eo < 100) {
        lid = f->p.brow_angle;
    }
    const bool has_lid = lid != PET_PACK_LID_OFF && !v->outline;
    int32_t h = has_lid ? hf : hf * eo / 100;
    if (h < 4 * Q) {
        h = 4 * Q;
    }
    if (v->outline) {
        rect_q(v, sclera, x, y + hf / 2 - h / 2, w, h, (w < h ? w : h) / 2, 0);
        to_outline(sclera);
        return;
    }
    const int32_t r = (w < h ? w : h) / 2;
    const bool dot = face->eye_style == PET_EYE_DOT;
    const bool ringed = face->eye_style == PET_EYE_RING;
    rect_q(v, sclera, x, y, w, h, r, v->def.sclera);
    if (ringed && sclera->kind == IT_RECT) {
        sclera->border = (uint8_t)clampi(px(v, w / RING_DIV), 2, 8);
        sclera->border_color = UI_PET_INK;
    }
    const int32_t vis_top = has_lid ? y + hf / 2 - hf * eo / 100 : y - h / 2;
    const int32_t vis_h = y + h / 2 - vis_top;
    const int32_t vis_cy = (vis_top + y + h / 2) / 2;
    const int32_t look_x = f->pdx;
    const int32_t look_y = f->pdy;
    if (face->eye_style == PET_EYE_SLIT) {
        int32_t pw = w * f->p.pupil_size / 100 * 46 / 100;
        if (pw < 7 * Q / 2) {
            pw = 7 * Q / 2;
        }
        int32_t ph = vis_h - 4 * Q;
        if (ph > w * 86 / 100) {
            ph = w * 86 / 100;
        }
        const int32_t tx = (w / 2 - pw / 2 - 2 * Q) * look_x / 100;
        int32_t room = (vis_h - ph) / 2 - Q;
        const int32_t ty = (room > 0 ? room : 0) * look_y / 100;
        rect_q(v, pupil, x + tx, vis_cy + ty, pw, ph, pw / 2, UI_PET_INK);
        circle_q(v, glint, x + tx + pw / 5 + 2 * Q, vis_cy + ty - ph * 26 / 100, 8 * Q / 5, UI_PET_WHITE);
    } else {
        const int32_t pr = w * f->p.pupil_size / 200 * (ringed ? RING_PUPIL_PCT : 100) / 100;
        const int32_t edge = ringed ? w / RING_DIV + Q : 2 * Q;
        const int32_t tx = (w / 2 - pr - edge) * look_x / 100;
        int32_t room = vis_h / 2 - pr - edge;
        const int32_t ty = (room > 0 ? room : 0) * look_y / 100;
        circle_q(v, pupil, x + tx, vis_cy + ty, dot ? w / 2 : pr, dot ? ink : UI_PET_INK);
        if (!ringed) {
            int32_t gr = pr * 28 / 100;
            if (gr < 3 * Q / 2) {
                gr = 3 * Q / 2;
            }
            circle_q(v, glint, x + tx + pr * 35 / 100, vis_cy + ty - pr * 35 / 100, gr, UI_PET_WHITE);
        }
    }
    if (dot) {
        hide(sclera);
    } else {
        pupil->clip = (int8_t)(sclera - v->items);
        glint->clip = pupil->clip;
    }
    if (has_lid) {
        const int32_t k = pet_sin(lid, w / 2) * 1000 / (pet_sin(90 - lid, 1000) > 0 ? pet_sin(90 - lid, 1000) : 1);
        const int32_t xi = x - sx * (w / 2 + Q);
        const int32_t xo = x + sx * (w / 2 + Q);
        const int32_t yi = vis_top + k;
        const int32_t yo = vis_top - k;
        const int32_t top = y - hf / 2 - 3 * Q;
        const uint32_t body = pet_pack_body_color(&v->def, v->col_cur);
        const int32_t a[3][2] = {{xi, top}, {xo, top}, {xo, yo}};
        const int32_t b[3][2] = {{xi, top}, {xo, yo}, {xi, yi}};
        tri_q(v, lid_a, a, body);
        tri_q(v, lid_b, b, body);
        const int32_t e[2][2] = {{xi, yi}, {xo, yo}};
        line_q(v, edge, e, 2, 4 * Q,
               pet_pack_dark(body) ? shade(body, UI_PET_WHITE, UI_PET_LIGHT_T) : shade(body, UI_PET_INK, UI_PET_DARK_T));
    }
}

static void build_brow(pet_view_t *v, const frame_t *f, int side, item_t *it)
{
    const pet_face_t *face = &v->def.face;
    const int32_t sx = side == 0 ? -1 : 1;
    const int32_t angle = f->p.brow_angle + (side == 0 ? f->p.brow_l : f->p.brow_r);
    const int32_t lift = (side == 0 ? (f->p.brow_lift < 0 ? -f->p.brow_lift : 0) : (f->p.brow_lift > 0 ? f->p.brow_lift : 0));
    if (!face->brows || v->outline || (v->rf.active && v->rf.eyes != PET_EYES_POSE) || abs(angle) < BROW_MIN_ANGLE ||
        f->eye_open < EYE_SHUT_OPEN) {
        hide(it);
        return;
    }
    const int32_t x = sx * face->eye_dx * Q + f->sway;
    const int32_t y = face->eye_y * Q + f->head_dy - f->lift;
    const int32_t by = y - f->p.eye_size * Q * 6 / 10 - 9 * Q - lift * Q;
    const int32_t ang = angle * -sx;
    const int32_t dx = pet_sin(ang + 90, 11 * Q);
    const int32_t dy = pet_sin(ang, 11 * Q);
    const int32_t pts[2][2] = {{x - dx, by - dy}, {x + dx, by + dy}};
    line_q(v, it, pts, 2, 4 * Q, face_ink(v));
}

/* Mouth: 10 items (tongue, line, 3 open-mouth triangles, a spare, hole, open tongue, 2 fangs). */
static void build_mouth(pet_view_t *v, const frame_t *f, item_t **its)
{
    for (int i = 0; i < 10; i++) {
        hide(its[i]);
    }
    const pet_face_t *face = &v->def.face;
    const int32_t curve = f->p.mouth_curve;
    int32_t opened = f->p.mouth_open;
    const int32_t hw = (f->p.mouth_w > 0 ? f->p.mouth_w : face->mouth_hw) * Q;
    const int32_t cx = f->p.mouth_dx * Q + f->sway;
    const int32_t y = face->mouth_y * Q + f->mouth_dy - f->lift;
    const int32_t sag = curve * hw * 6 / 1000;
    const int32_t y0 = y - sag / 2;
    const uint32_t ink = v->outline ? UI_PET_NIGHT : face_ink(v);
    const int32_t sw = (v->outline ? 2 : 4) * Q;
    if (opened > 5) {
        if (curve > 40 && !v->outline) {
            const int32_t hh = 8 * Q + opened * Q * 16 / 100;
            const int32_t p0[2] = {cx - hw, y0 - 2 * Q}, c[2] = {cx, y0 + 2 * hh}, p2[2] = {cx + hw, y0 - 2 * Q};
            int32_t pts[5][2];
            quad(pts, 5, p0, c, p2);
            /* The open smile: a fan from the left corner between the curve and the chord. */
            for (int i = 1; i < 4; i++) {
                const int32_t t[3][2] = {{pts[0][0], pts[0][1]}, {pts[i][0], pts[i][1]}, {pts[i + 1][0], pts[i + 1][1]}};
                tri_q(v, its[1 + i], t, ink);
            }
            rect_q(v, its[7], cx, y0 + hh - Q, 14 * Q, 7 * Q, 7 * Q / 2, UI_PET_TONGUE);
            return;
        }
        const int32_t mh = 6 * Q + opened * Q / 5;
        const int32_t mw = 14 * Q + opened * Q / 10;
        rect_q(v, its[6], cx, y, mw, mh, mh / 2 < 7 * Q ? mh / 2 : 7 * Q, ink);
        if (v->outline) {
            to_outline(its[6]);
        }
        return;
    }
    const bool tongue = f->p.accessory == PET_ACC_TONGUE && !(face->acc_mask & (1u << PET_ACC_TONGUE)) && !v->outline;
    if (tongue) {
        rect_q(v, its[0], cx + hw - 4 * Q, y0 + sag * 9 / 10 + 7 * Q / 2, 10 * Q, 9 * Q, 9 * Q / 2, UI_PET_TONGUE);
    }
    const int32_t p0[2] = {cx - hw, y0}, c[2] = {cx, y0 + 2 * sag}, p2[2] = {cx + hw, y0};
    int32_t pts[MOUTH_POINTS][2];
    quad(pts, MOUTH_POINTS, p0, c, p2);
    line_q(v, its[1], (const int32_t(*)[2])pts, MOUTH_POINTS, sw, ink);
    if (face->mouth_set == PET_MOUTH_SNOUT && !v->outline) {
        const int32_t fy = y0 + sag * 45 / 100;
        for (int s = 0; s < 2; s++) {
            const int32_t fx = (s == 0 ? -9 : 9) * Q + cx;
            const int32_t t[3][2] = {{fx - 7 * Q / 2, fy}, {fx, fy + 7 * Q}, {fx + 7 * Q / 2, fy}};
            tri_q(v, its[8 + s], t, UI_PET_WHITE);
        }
    }
}

static void drop_q(pet_view_t *v, item_t *tri_it, item_t *circ_it, int32_t x, int32_t y, int32_t s)
{
    const int32_t t[3][2] = {{x - s * 7 / 10, y + s / 5}, {x, y - s}, {x + s * 7 / 10, y + s / 5}};
    tri_q(v, tri_it, t, UI_PET_DROP);
    circle_q(v, circ_it, x, y + s * 35 / 100, s * 7 / 10, UI_PET_DROP);
}

static void star_q(pet_view_t *v, item_t **its, int32_t x, int32_t y, int32_t r)
{
    const int32_t k = r * 28 / 100;
    const int32_t spikes[4][3][2] = {
        {{x, y - r}, {x - k, y}, {x + k, y}},
        {{x, y + r}, {x - k, y}, {x + k, y}},
        {{x - r, y}, {x, y - k}, {x, y + k}},
        {{x + r, y}, {x, y - k}, {x, y + k}},
    };
    for (int i = 0; i < 4; i++) {
        tri_q(v, its[i], spikes[i], UI_PET_SPARKLE);
    }
}

static void heart_q(pet_view_t *v, item_t **its, int32_t x, int32_t y, int32_t s)
{
    circle_q(v, its[0], x - s / 2, y - s * 3 / 10, s * 55 / 100, UI_PET_HEART);
    circle_q(v, its[1], x + s / 2, y - s * 3 / 10, s * 55 / 100, UI_PET_HEART);
    const int32_t t[3][2] = {{x - s * 105 / 100, y - s / 10}, {x + s * 105 / 100, y - s / 10}, {x, y + s}};
    tri_q(v, its[2], t, UI_PET_HEART);
}

#define FX_ITEMS  36

/* Accessories and reaction effects: FX_ITEMS items in a fixed order. */
static void build_fx(pet_view_t *v, const frame_t *f, uint32_t now, item_t **its)
{
    for (int i = 0; i < FX_ITEMS; i++) {
        hide(its[i]);
    }
    const pet_face_t *face = &v->def.face;
    const int acc = f->p.accessory;
    const bool own = acc > 0 && acc < PET_ACC_COUNT && (face->acc_mask & (1u << acc));
    const int32_t lift = f->lift;
    if (v->outline) {
        if (acc == PET_ACC_ZZZ && !own) {
            static const int8_t Z[3][3] = {{62, -52, 8}, {76, -70, 10}, {90, -92, 13}};
            for (int i = 0; i < 3; i++) {
                const int32_t x = Z[i][0] * Q, y = Z[i][1] * Q - lift, s = Z[i][2] * Q;
                const int32_t pts[4][2] = {{x, y}, {x + s, y}, {x, y + s}, {x + s, y + s}};
                line_q(v, its[i], pts, 4, 2 * Q, UI_PET_NIGHT);
            }
        }
        return;
    }
    if (!own) {
        switch (acc) {
        case PET_ACC_ZZZ: {
            static const int8_t Z[3][3] = {{62, -52, 8}, {76, -70, 10}, {90, -92, 13}};
            for (int i = 0; i < 3; i++) {
                const int32_t x = Z[i][0] * Q, y = Z[i][1] * Q - lift, s = Z[i][2] * Q;
                const int32_t pts[4][2] = {{x, y}, {x + s, y}, {x, y + s}, {x + s, y + s}};
                line_q(v, its[i], pts, 4, 3 * Q, UI_PET_FOG);
            }
            break;
        }
        case PET_ACC_SWEAT:
            drop_q(v, its[3], its[4], (face->eye_dx + 37) * Q + f->sway, face->sweat_y * Q + f->head_dy - lift, 8 * Q);
            break;
        case PET_ACC_TEAR: {
            const int32_t fall = v->frozen ? 0 : (int32_t)(now % TEAR_FALL_MS) * 14 * Q / TEAR_FALL_MS;
            drop_q(v, its[5], its[6], -face->eye_dx * Q + f->sway, (face->eye_y + 26) * Q + f->head_dy - lift + fall,
                   6 * Q);
            break;
        }
        case PET_ACC_SPARKLES:
            break;
        case PET_ACC_DOTS:
            for (int i = 0; i < 3; i++) {
                const bool shown = v->frozen || (int32_t)(now % (4 * DOTS_STEP_MS)) >= i * DOTS_STEP_MS;
                if (shown) {
                    circle_q(v, its[19 + i], (64 + 13 * i) * Q, (-66 - 9 * i) * Q - lift, (3 + i) * Q, UI_PET_CREAM);
                }
            }
            break;
        default:
            break;
        }
    }
    const bool sparkles = (acc == PET_ACC_SPARKLES && !own) || (v->rf.active && v->rf.fx == PET_FX_SPARKLES);
    if (sparkles) {
        static const int8_t S[3][3] = {{-88, -44, 9}, {88, -58, 11}, {96, 0, 6}};
        for (int i = 0; i < 3; i++) {
            star_q(v, &its[7 + 4 * i], S[i][0] * Q, S[i][1] * Q - lift, S[i][2] * Q);
        }
    }
    if (!v->rf.active) {
        return;
    }
    switch (v->rf.fx) {
    case PET_FX_HEARTS: {
        const int32_t t = v->rf.fx_permille;
        const int32_t rise = 12 * Q * t / 1000;
        const int32_t k = 1000 - t * 6 / 10;
        heart_q(v, &its[22], 78 * Q, -62 * Q - rise - lift, 8 * Q * k / 1000);
        heart_q(v, &its[25], 96 * Q, -38 * Q - rise - lift, 5 * Q * k / 1000);
        break;
    }
    case PET_FX_IMPACT: {
        static const int8_t T[4][4] = {{-84, -34, -96, -44}, {-90, -14, -104, -14}, {84, -34, 96, -44}, {90, -14, 104, -14}};
        for (int i = 0; i < 4; i++) {
            const int32_t pts[2][2] = {{T[i][0] * Q, T[i][1] * Q - lift}, {T[i][2] * Q, T[i][3] * Q - lift}};
            line_q(v, its[28 + i], pts, 2, 3 * Q, UI_PET_CREAM);
        }
        break;
    }
    case PET_FX_BANG: {
        /* Pops in by 2 px steps. */
        const int32_t r = ((15 * (int32_t)v->rf.fx_permille / 1000) & ~1) * Q;
        if (r > 0) {
            circle_q(v, its[32], 80 * Q, -62 * Q - lift, r, UI_PET_YELLOW);
            if (v->rf.fx_permille >= 1000) {
                rect_q(v, its[33], 80 * Q, -132 * Q / 2 - lift, 5 * Q, 13 * Q, 5 * Q / 2, UI_PET_INK);
                circle_q(v, its[34], 80 * Q, -54 * Q - lift, 3 * Q, UI_PET_INK);
            }
        }
        break;
    }
    case PET_FX_ZZZ1: {
        const int32_t x = 66 * Q, y = -56 * Q - lift, s = 9 * Q;
        const int32_t pts[4][2] = {{x, y}, {x + s, y}, {x, y + s}, {x + s, y + s}};
        line_q(v, its[35], pts, 4, 3 * Q, UI_PET_SLATE);
        break;
    }
    default:
        break;
    }
}

/* ---- the frame ---- */

static void step_animation(pet_view_t *v, uint32_t now, frame_t *f)
{
    if (v->tween < 1000) {
        const uint32_t t = now - v->tween_start;
        v->tween = v->frozen || t >= PET_TWEEN_MS ? 1000 : (uint16_t)(t * 1000 / PET_TWEEN_MS);
        pet_pose_lerp(&v->from, &v->to, v->tween, &v->cur);
        v->col_cur = pet_color_lerp(v->col_from, v->col_to, v->tween);
        v->drop_cur = (int16_t)(v->drop_from + (v->drop_to - v->drop_from) * (int32_t)pet_ease_out(v->tween) / 1000);
    }
    pet_aux_lerp(v->aux_from, v->aux_to, v->tween, v->aux_cur, v->aux_sel);

    /* The reaction blends its pose and aux row over the mood's. */
    v->rf = (pet_react_frame_t){.lids = 1000};
    if (v->react != PET_REACT_NONE) {
        const uint32_t t = v->frozen ? FROZEN_AT[v->react] : now - v->react_start;
        pet_react_sample(v->react, t, &v->rf);
        if (!v->rf.active) {
            v->react = PET_REACT_NONE;
        }
    }
    f->p = v->cur;
    /* Attention lights a pet at rest in its happy colour (mockup 15). */
    if (v->rf.active && v->react == PET_REACT_ATTENTION && (v->mood == MOOD_IDLE || v->mood == MOOD_SLEEPING)) {
        v->col_cur = pet_color_lerp(v->col_to, pet_pack_mood_color(&v->def, MOOD_HAPPY), v->rf.weight);
    } else if (v->tween >= 1000) {
        v->col_cur = v->col_to;
    }
    if (v->rf.active) {
        pet_pose_t rp;
        pet_pose_react(&v->def, v->react, &v->cur, &rp);
        rp.mouth_open = (int16_t)(rp.mouth_open * v->rf.mouth / 1000);
        pet_pose_lerp(&v->cur, &rp, v->rf.weight, &f->p);
        const int row = pet_react_aux_row(v->react);
        if (row >= 0) {
            int16_t target[PET_PACK_AUX_COUNT], cur[PET_PACK_AUX_COUNT], sel[PET_PACK_AUX_COUNT];
            for (int a = 0; a < PET_PACK_AUX_COUNT; a++) {
                target[a] = v->def.aux[row][a];
            }
            pet_aux_lerp(v->aux_cur, target, v->rf.weight, cur, sel);
            memcpy(v->aux_cur, cur, sizeof(cur));
            for (int a = 0; a < PET_PACK_AUX_COUNT; a++) {
                if (v->rf.weight >= 500) {
                    v->aux_sel[a] = sel[a];
                }
            }
        }
    }

    /* Blink, glance, bob, jitter. */
    if (v->frozen) {
        v->blinking = false;
    } else if (v->blinking && now - v->blink_start >= PET_BLINK_MS) {
        v->blinking = false;
        schedule_blink(v, now);
    } else if (!v->blinking && v->blink_at != 0 && reached(v->blink_at, now)) {
        v->blinking = true;
        v->blink_start = now;
    } else if (!v->blinking && v->blink_at == 0 && f->p.blink_min_ms > 0) {
        schedule_blink(v, now);
    }
    pet_glance_set_mode(&v->glance, v->frozen ? PET_GLANCE_OFF : (pet_glance_mode_t)f->p.glance, now);
    pet_glance_update(&v->glance, now);

    v->bob_q = 0;
    v->bob_permille = 0;
    if (!v->frozen && f->p.bob_amp_px > 0 && f->p.bob_period_ms > 0) {
        const uint32_t period = (uint32_t)f->p.bob_period_ms;
        const int32_t deg = (int32_t)((now % period) * 360 / period);
        v->bob_permille = (int16_t)pet_sin(deg, 1000);
        v->bob_q = (int16_t)pet_sin(deg, f->p.bob_amp_px * Q);
    }
    if (v->frozen || f->p.jitter_px == 0) {
        v->jitter = 0;
    } else if (reached(v->jitter_at, now)) {
        v->jitter_at = now + JITTER_STEP_MS;
        v->jitter = (int16_t)((int32_t)(esp_random() % (uint32_t)(2 * f->p.jitter_px + 1)) - f->p.jitter_px);
    }

    /* Oscillators of the mood, or of the reaction while it shows. */
    if (!v->frozen) {
        const uint8_t react_id = v->rf.active && v->rf.weight >= 500 ? pet_react_pack_id(v->react) : 0;
        const uint8_t id = react_id != 0 ? react_id : (uint8_t)v->mood;
        const int16_t scan = (int16_t)(v->glance.dx * 1000 / PET_GLANCE_RANGE);
        for (uint8_t i = 0; i < v->def.n_osc && i < PET_PACK_MAX_OSC; i++) {
            const pet_osc_t *o = &v->def.osc[i];
            if (o->mood != id) {
                continue;
            }
            if (o->wave == PET_WAVE_JITTER) {
                if (reached(v->osc_at[i], now)) {
                    v->osc_at[i] = now + o->period_ms;
                    v->osc_jitter[i] = pet_osc_value(o, now, 0, 0, esp_random());
                }
                v->aux_cur[o->aux] = (int16_t)(v->aux_cur[o->aux] + v->osc_jitter[i]);
            } else {
                v->aux_cur[o->aux] = (int16_t)(v->aux_cur[o->aux] + pet_osc_value(o, now, v->bob_permille, scan, 0));
            }
        }
    }

    /* Lift for the sticky banner, 1 px per step. */
    if (v->lift_px != v->lift_target && reached(v->lift_at, now)) {
        v->lift_px = (int16_t)(v->lift_px + (v->lift_target > v->lift_px ? 1 : -1));
        v->lift_at = now + LIFT_STEP_MS;
    }

    int32_t open = f->p.eye_open;
    if (v->blinking) {
        const int32_t half = PET_BLINK_MS / 2;
        open = open * abs((int32_t)(now - v->blink_start) - half) / half;
    }
    f->eye_open = open * v->rf.lids / 1000;
    f->pdx = (int16_t)clampi(f->p.pupil_dx + v->glance.dx, -100, 100);
    f->pdy = (int16_t)clampi(f->p.pupil_dy + v->glance.dy, -100, 100);
    f->hop = v->rf.hop * Q;
    f->squish = (v->drop_cur + v->rf.squish) * Q;
    f->sway = (v->jitter + v->rf.sway) * Q;
    f->body_dy = v->bob_q - f->hop;
    f->head_dy = f->body_dy + f->squish * v->def.face.eye_sq / 16;
    f->mouth_dy = f->body_dy + f->squish * v->def.face.mouth_sq / 16;
    f->lift = v->lift_px * Q * 16 / (v->scale > 0 ? v->scale : 16);
}

static uint32_t hash_item(const item_t *it)
{
    if (it->kind == IT_HIDDEN) {
        return 0;
    }
    const uint8_t *b = (const uint8_t *)it;
    uint32_t h = 2166136261u;
    const size_t n = offsetof(item_t, p) + sizeof(ipt_t) * (it->kind == IT_RECT ? 2 : it->n);
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h | 1u;
}

static box_t item_box(const item_t *it)
{
    box_t b = {0};
    if (it->kind == IT_HIDDEN) {
        return b;
    }
    if (it->kind == IT_RECT) {
        b = (box_t){it->p[0].x, it->p[0].y, it->p[0].x + it->p[1].x - 1, it->p[0].y + it->p[1].y - 1, true};
        return b;
    }
    b = (box_t){INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN, true};
    for (int i = 0; i < it->n; i++) {
        b.x1 = it->p[i].x < b.x1 ? it->p[i].x : b.x1;
        b.y1 = it->p[i].y < b.y1 ? it->p[i].y : b.y1;
        b.x2 = it->p[i].x > b.x2 ? it->p[i].x : b.x2;
        b.y2 = it->p[i].y > b.y2 ? it->p[i].y : b.y2;
    }
    const int32_t pad = it->kind == IT_LINE ? it->width / 2 + 1 : 1;
    b.x1 -= pad;
    b.y1 -= pad;
    b.x2 += pad;
    b.y2 += pad;
    return b;
}

static void add_dirty(box_t *d, int *n, box_t b)
{
    if (!b.valid) {
        return;
    }
    for (int i = 0; i < *n; i++) {
        if (b.x1 <= d[i].x2 + DIRTY_JOIN_PX && b.x2 >= d[i].x1 - DIRTY_JOIN_PX && b.y1 <= d[i].y2 + DIRTY_JOIN_PX &&
            b.y2 >= d[i].y1 - DIRTY_JOIN_PX) {
            d[i].x1 = b.x1 < d[i].x1 ? b.x1 : d[i].x1;
            d[i].y1 = b.y1 < d[i].y1 ? b.y1 : d[i].y1;
            d[i].x2 = b.x2 > d[i].x2 ? b.x2 : d[i].x2;
            d[i].y2 = b.y2 > d[i].y2 ? b.y2 : d[i].y2;
            return;
        }
    }
    if (*n < MAX_DIRTY) {
        d[(*n)++] = b;
        return;
    }
    /* Full: grow the rectangle that grows least. */
    int best = 0;
    int64_t best_cost = INT64_MAX;
    for (int i = 0; i < *n; i++) {
        const int64_t w = (b.x2 > d[i].x2 ? b.x2 : d[i].x2) - (b.x1 < d[i].x1 ? b.x1 : d[i].x1);
        const int64_t h = (b.y2 > d[i].y2 ? b.y2 : d[i].y2) - (b.y1 < d[i].y1 ? b.y1 : d[i].y1);
        const int64_t cost = w * h - (int64_t)(d[i].x2 - d[i].x1) * (d[i].y2 - d[i].y1);
        if (cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    box_t *u = &d[best];
    u->x1 = b.x1 < u->x1 ? b.x1 : u->x1;
    u->y1 = b.y1 < u->y1 ? b.y1 : u->y1;
    u->x2 = b.x2 > u->x2 ? b.x2 : u->x2;
    u->y2 = b.y2 > u->y2 ? b.y2 : u->y2;
}

static void render(pet_view_t *v, uint32_t now)
{
    frame_t f;
    step_animation(v, now, &f);
    v->n_items = 0;
    for (int i = 0; i < v->def.n_parts; i++) {
        const int sides = v->def.parts[i].mirrored ? 2 : 1;
        for (int s = 0; s < sides; s++) {
            build_part(v, &f, i, s, slot(v));
        }
    }
    item_t *face[7 * 2 + 2 + 10 + FX_ITEMS];
    for (size_t i = 0; i < sizeof(face) / sizeof(face[0]); i++) {
        face[i] = slot(v);
    }
    if (face[sizeof(face) / sizeof(face[0]) - 1] != NULL) {
        build_eye(v, &f, 0, &face[0]);
        build_eye(v, &f, 1, &face[7]);
        build_brow(v, &f, 0, face[14]);
        build_brow(v, &f, 1, face[15]);
        build_mouth(v, &f, &face[16]);
        build_fx(v, &f, now, &face[26]);
    }

    box_t dirty[MAX_DIRTY];
    int n_dirty = 0;
    uint16_t changed = 0;
    for (uint16_t i = 0; i < MAX_ITEMS; i++) {
        const bool used = i < v->n_items;
        const uint32_t h = used ? hash_item(&v->items[i]) : 0;
        const box_t b = used ? item_box(&v->items[i]) : (box_t){0};
        if (h == v->prev_hash[i]) {
            continue;
        }
        changed++;
        add_dirty(dirty, &n_dirty, v->prev_box[i]);
        add_dirty(dirty, &n_dirty, b);
        v->prev_hash[i] = h;
        v->prev_box[i] = b;
    }
    if (n_dirty > 0) {
        lv_area_t base;
        lv_obj_get_coords(v->obj, &base);
        for (int i = 0; i < n_dirty; i++) {
            lv_area_t a = {base.x1 + dirty[i].x1, base.y1 + dirty[i].y1, base.x1 + dirty[i].x2, base.y1 + dirty[i].y2};
            lv_obj_invalidate_area(v->obj, &a);
        }
    }
    v->dirty_count = changed;

    v->state = (ui_pet_state_t){
        .mood = v->mood,
        .pose = v->cur,
        .tween_permille = v->tween,
        .color = v->col_cur,
        .eye_open = (int16_t)f.eye_open,
        .pupil_dx = f.pdx,
        .pupil_dy = f.pdy,
        .bob_px = (int16_t)px(v, v->bob_q),
        .jitter_px = v->jitter,
        .blinking = v->blinking,
        .reaction = v->react,
        .frozen = v->frozen,
        .outline = v->outline,
        .scale = v->scale,
        .animating = v->visible && v->powered,
        .items = v->n_items,
        .dirty = changed,
        .slow = v->slow,
    };
    memcpy(v->state.id, v->def.id, sizeof(v->state.id));
    memcpy(v->state.aux, v->aux_cur, sizeof(v->state.aux));
    memcpy(v->state.aux_sel, v->aux_sel, sizeof(v->state.aux_sel));
}

/* ---- drawing ---- */

static lv_color_t col(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static void draw_item(lv_layer_t *layer, const item_t *it, int32_t bx, int32_t by)
{
    switch (it->kind) {
    case IT_RECT: {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = it->radius;
        d.bg_opa = it->fill ? LV_OPA_COVER : LV_OPA_TRANSP;
        d.bg_color = col(it->color);
        if (it->border > 0) {
            d.border_width = it->border;
            d.border_color = col(it->border_color);
            d.border_opa = LV_OPA_COVER;
        }
        const lv_area_t a = {bx + it->p[0].x, by + it->p[0].y, bx + it->p[0].x + it->p[1].x - 1,
                             by + it->p[0].y + it->p[1].y - 1};
        lv_draw_rect(layer, &d, &a);
        break;
    }
    case IT_LINE: {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = col(it->color);
        d.width = it->width;
        d.round_start = 1;
        d.round_end = 1;
        for (int i = 0; i + 1 < it->n; i++) {
            d.p1 = (lv_point_precise_t){bx + it->p[i].x, by + it->p[i].y};
            d.p2 = (lv_point_precise_t){bx + it->p[i + 1].x, by + it->p[i + 1].y};
            lv_draw_line(layer, &d);
        }
        break;
    }
    case IT_TRI: {
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        d.color = col(it->color);
        d.opa = LV_OPA_COVER;
        for (int i = 0; i < 3; i++) {
            d.p[i] = (lv_point_precise_t){bx + it->p[i].x, by + it->p[i].y};
        }
        lv_draw_triangle(layer, &d);
        break;
    }
    default:
        break;
    }
}

static bool area_and(lv_area_t *out, const lv_area_t *a, const lv_area_t *b)
{
    out->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    out->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    out->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    out->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return out->x1 <= out->x2 && out->y1 <= out->y2;
}

/* Fewer covered rows than this are not worth a second draw call. */
#define COVERED_MIN_ROWS  4

/*
 * The rows of rect item i that a later plain fill covers over the item's whole width (the dome's lower
 * arc under the skirt): nothing drawn there can show, and a rounded rect costs a masked blend per row.
 */
static bool covered_rows(const pet_view_t *v, uint16_t i, int32_t *y1, int32_t *y2)
{
    const item_t *a = &v->items[i];
    const int32_t ax2 = a->p[0].x + a->p[1].x;
    const int32_t ay2 = a->p[0].y + a->p[1].y - 1;
    int32_t best = COVERED_MIN_ROWS - 1;
    for (uint16_t j = i + 1; j < v->n_items; j++) {
        const item_t *b = &v->items[j];
        if (b->kind != IT_RECT || !b->fill || b->border != 0 || b->clip >= 0) {
            continue;
        }
        if (b->p[0].x > a->p[0].x || b->p[0].x + b->p[1].x < ax2) {
            continue;
        }
        /* Between its rounded corners a fill is solid from edge to edge. */
        const int32_t top = b->p[0].y + b->radius > a->p[0].y ? b->p[0].y + b->radius : a->p[0].y;
        const int32_t low = b->p[0].y + b->p[1].y - 1 - b->radius;
        const int32_t bottom = low < ay2 ? low : ay2;
        if (bottom - top + 1 > best) {
            best = bottom - top + 1;
            *y1 = top;
            *y2 = bottom;
        }
    }
    return best >= COVERED_MIN_ROWS;
}

static void on_draw(lv_event_t *e)
{
    pet_view_t *v = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(v->obj, &base);
    const lv_area_t clip = layer->_clip_area;
    for (uint16_t i = 0; i < v->n_items; i++) {
        const item_t *it = &v->items[i];
        if (it->kind == IT_HIDDEN || !v->prev_box[i].valid) {
            continue;
        }
        const box_t *b = &v->prev_box[i];
        lv_area_t a = {base.x1 + b->x1, base.y1 + b->y1, base.x1 + b->x2, base.y1 + b->y2};
        lv_area_t visible;
        if (!area_and(&visible, &a, &clip)) {
            continue;
        }
        if (it->clip >= 0) {
            const box_t *cb = &v->prev_box[it->clip];
            const lv_area_t ca = {base.x1 + cb->x1, base.y1 + cb->y1, base.x1 + cb->x2, base.y1 + cb->y2};
            lv_area_t clipped;
            if (!cb->valid || !area_and(&clipped, &ca, &clip)) {
                continue;
            }
            layer->_clip_area = clipped;
            draw_item(layer, it, base.x1, base.y1);
            layer->_clip_area = clip;
        } else {
            int32_t y1, y2;
            if (it->kind != IT_RECT || !covered_rows(v, i, &y1, &y2)) {
                draw_item(layer, it, base.x1, base.y1);
                continue;
            }
            const lv_area_t above = {clip.x1, clip.y1, clip.x2, base.y1 + y1 - 1};
            const lv_area_t below = {clip.x1, base.y1 + y2 + 1, clip.x2, clip.y2};
            const lv_area_t *parts[] = {&above, &below};
            for (int k = 0; k < 2; k++) {
                lv_area_t rows;
                if (area_and(&rows, parts[k], &visible)) {
                    layer->_clip_area = rows;
                    draw_item(layer, it, base.x1, base.y1);
                }
            }
            layer->_clip_area = clip;
        }
    }
}

static void on_frame(lv_timer_t *t)
{
    pet_view_t *v = lv_timer_get_user_data(t);
    const uint32_t recent = ui_frame_recent_x10();
    if (!v->slow && recent > SLOW_ABOVE_X10 && v->frame_ms < FRAME_SLOW_MS) {
        v->slow = true;
        lv_timer_set_period(t, FRAME_SLOW_MS);
    } else if (v->slow && recent < FAST_BELOW_X10) {
        v->slow = false;
        lv_timer_set_period(t, v->frame_ms);
    }
    render(v, lv_tick_get());
}

static void update_timer(pet_view_t *v)
{
    const uint32_t now = lv_tick_get();
    if (v->visible && v->powered) {
        /* Blink and glance restart from now rather than catching up on the time spent hidden. */
        schedule_blink(v, now);
        pet_glance_init(&v->glance, rand_cb, NULL, now);
        render(v, now);
        lv_timer_resume(v->timer);
    } else {
        lv_timer_pause(v->timer);
        v->blinking = false;
        render(v, now);
    }
}

static void place(pet_view_t *v)
{
    const int32_t w = BOX_W * v->scale / 16;
    const int32_t h = BOX_H * v->scale / 16;
    v->ox = w / 2;
    v->oy = BOX_CY * v->scale / 16;
    lv_obj_set_pos(v->obj, v->cx - v->ox, v->cy - v->oy);
    lv_obj_set_size(v->obj, w, h);
    memset(v->prev_hash, 0, sizeof(v->prev_hash));
    memset(v->prev_box, 0, sizeof(v->prev_box));
    lv_obj_invalidate(v->obj);
}

static void land_mood(pet_view_t *v, proto_mood_t mood, bool tween, uint32_t now)
{
    pet_pose_for_pet(&v->def, mood, &v->to);
    v->col_to = pet_pack_mood_color(&v->def, (unsigned)mood);
    for (int a = 0; a < PET_PACK_AUX_COUNT; a++) {
        v->aux_to[a] = v->def.aux[mood][a];
    }
    v->drop_to = mood == MOOD_SLEEPING ? v->def.face.sleep_drop : 0;
    if (!tween) {
        v->from = v->cur = v->to;
        v->col_from = v->col_cur = v->col_to;
        memcpy(v->aux_from, v->aux_to, sizeof(v->aux_from));
        v->drop_from = v->drop_cur = v->drop_to;
        v->tween = 1000;
    } else {
        v->from = v->cur;
        v->col_from = v->col_cur;
        memcpy(v->aux_from, v->aux_cur, sizeof(v->aux_from));
        v->drop_from = v->drop_cur;
        v->tween = 0;
        v->tween_start = now;
    }
}

pet_view_t *pet_view_create(lv_obj_t *parent, int32_t cx, int32_t cy, uint8_t scale)
{
    pet_view_t *v = heap_caps_calloc(1, sizeof(*v), MALLOC_CAP_SPIRAM);
    if (v == NULL) {
        v = calloc(1, sizeof(*v));
    }
    if (v == NULL) {
        return NULL;
    }
    v->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(v->obj);
    lv_obj_remove_flag(v->obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v->obj, UI_FLAG_NO_TEXT);
    lv_obj_add_event_cb(v->obj, on_draw, LV_EVENT_DRAW_MAIN, v);
    v->cx = cx;
    v->cy = cy;
    v->scale = scale;
    v->powered = s_powered;
    place(v);
    for (int i = 0; i < MAX_VIEWS; i++) {
        if (s_views[i] == NULL) {
            s_views[i] = v;
            break;
        }
    }
    v->frame_ms = FRAME_MS;
    v->timer = lv_timer_create(on_frame, FRAME_MS, v);
    lv_timer_pause(v->timer);
    pet_glance_init(&v->glance, rand_cb, NULL, lv_tick_get());
    pet_view_set_def(v, ui_pet_def());
    return v;
}

void pet_view_set_def(pet_view_t *v, const pet_def_t *def)
{
    if (v == NULL || def == NULL) {
        return;
    }
    v->def = *def;
    v->react = PET_REACT_NONE;
    memset(v->anchor_q, 0, sizeof(v->anchor_q));
    land_mood(v, v->has_mood ? v->mood : MOOD_IDLE, false, lv_tick_get());
    v->has_mood = true;
    lv_obj_invalidate(v->obj);
    render(v, lv_tick_get());
}

void pet_view_set_scale(pet_view_t *v, uint8_t scale)
{
    if (v == NULL || scale < 4 || scale > 16 || scale == v->scale) {
        return;
    }
    v->scale = scale;
    place(v);
    render(v, lv_tick_get());
}

void pet_view_set_mood(pet_view_t *v, proto_mood_t mood)
{
    if (v == NULL) {
        return;
    }
    if (mood >= MOOD_COUNT) {
        mood = MOOD_IDLE;
    }
    if (v->has_mood && mood == v->mood) {
        return;
    }
    const uint32_t now = lv_tick_get();
    land_mood(v, mood, v->has_mood && !v->frozen, now);
    v->has_mood = true;
    v->mood = mood;
    if (v->cur.blink_min_ms == 0 || v->to.blink_min_ms == 0) {
        v->blinking = false;
    }
    v->blink_at = 0;
    render(v, now);
}

void pet_view_set_visible(pet_view_t *v, bool visible)
{
    if (v == NULL) {
        return;
    }
    ui_set_hidden(v->obj, !visible);
    if (visible != v->visible) {
        v->visible = visible;
        update_timer(v);
    }
}

void pet_view_set_powered(pet_view_t *v, bool powered)
{
    if (v != NULL && powered != v->powered) {
        v->powered = powered;
        update_timer(v);
    }
}

void pet_view_set_frame_ms(pet_view_t *v, uint32_t ms)
{
    if (v == NULL || ms == v->frame_ms) {
        return;
    }
    v->frame_ms = ms;
    v->slow = false;
    lv_timer_set_period(v->timer, ms);
}

void pet_view_set_frozen(pet_view_t *v, bool frozen)
{
    if (v == NULL) {
        return;
    }
    v->frozen = frozen;
    if (frozen) {
        v->tween = 1000;
        v->cur = v->to;
        v->col_cur = v->col_to;
        v->drop_cur = v->drop_to;
        memcpy(v->aux_from, v->aux_to, sizeof(v->aux_from));
    }
    render(v, lv_tick_get());
}

void pet_view_set_outline(pet_view_t *v, bool outline)
{
    if (v != NULL && outline != v->outline) {
        v->outline = outline;
        render(v, lv_tick_get());
    }
}

void pet_view_set_lift(pet_view_t *v, int16_t px_up)
{
    if (v != NULL && px_up != v->lift_target) {
        v->lift_target = px_up;
        if (v->frozen || !v->visible || !v->powered) {
            v->lift_px = px_up;
        }
        render(v, lv_tick_get());
    }
}

void pet_view_react(pet_view_t *v, pet_react_t r)
{
    if (v == NULL || (unsigned)r >= PET_REACT_COUNT) {
        return;
    }
    const uint32_t now = lv_tick_get();
    if (r == PET_REACT_NONE && v->react != PET_REACT_NONE) {
        /* Back to the mood with the usual tween instead of a jump. */
        v->from = v->cur;
        v->col_from = v->col_cur;
        memcpy(v->aux_from, v->aux_cur, sizeof(v->aux_from));
        v->drop_from = v->drop_cur;
        v->tween = 0;
        v->tween_start = now;
    }
    v->react = r;
    v->react_start = now;
    render(v, now);
}

void pet_view_get_state(const pet_view_t *v, ui_pet_state_t *out)
{
    if (v == NULL) {
        *out = (ui_pet_state_t){0};
        return;
    }
    *out = v->state;
}

void pet_view_follow(pet_view_t *v)
{
    if (v != NULL) {
        v->follow = true;
        pet_view_set_def(v, ui_pet_def());
    }
}

void pet_views_set_powered(bool powered)
{
    s_powered = powered;
    for (int i = 0; i < MAX_VIEWS; i++) {
        pet_view_set_powered(s_views[i], powered);
    }
}

void ui_pet_set_def(const pet_def_t *def)
{
    if (def == NULL || !ui_lock()) {
        return;
    }
    s_def = *def;
    s_def_ready = true;
    for (int i = 0; i < MAX_VIEWS; i++) {
        if (s_views[i] != NULL && s_views[i]->follow) {
            pet_view_set_def(s_views[i], &s_def);
        }
    }
    ui_unlock();
}
