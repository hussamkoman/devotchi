#pragma once

#include "proto.h"

#include "lvgl.h"

#include <stdbool.h>

/* Each returns its screen object; called once from ui_init() with the lock held. */
lv_obj_t *screen_splash_create(void);
lv_obj_t *screen_provisioning_create(void);
lv_obj_t *screen_wifi_create(void);
lv_obj_t *screen_connecting_create(void);
lv_obj_t *screen_pairing_create(void);
lv_obj_t *screen_pet_create(void);
lv_obj_t *screen_approval_create(void);
lv_obj_t *screen_identify_create(void);

/* Status bar, toast and banner live on lv_layer_top(); created once from ui_init(). */
void ui_status_bar_create(void);
void ui_status_bar_set_visible(bool visible);
void ui_overlay_create(void);

/** Face of the pet inside parent; the rest of the ui only uses these two calls. */
void pet_view_create(lv_obj_t *parent);
/** Caller holds the ui lock. */
void pet_view_set_mood(proto_mood_t mood);

/** Marks a label whose text ui_get_texts() must not reveal (the provisioning proof-of-possession). */
#define UI_FLAG_SECRET  LV_OBJ_FLAG_USER_1
/** Marks an object whose labels ui_get_texts() skips because the screen command reports them separately. */
#define UI_FLAG_NO_TEXT LV_OBJ_FLAG_USER_2

/** Longest label text ui_label_set() keeps (a details page: 6 x 38 characters and 5 newlines). */
#define UI_LABEL_TEXT_SIZE  256

/** Screen object with the theme background and no scrolling. */
lv_obj_t *ui_screen_base_create(void);

/** Label with a fixed width at (UI_PAD, y); long text wraps or, with one_line, ends in dots. */
lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t y, bool one_line);

/** Reduces text to printable ASCII and updates the label only when it changed. Caller holds the ui lock. */
void ui_label_set(lv_obj_t *label, const char *text);
