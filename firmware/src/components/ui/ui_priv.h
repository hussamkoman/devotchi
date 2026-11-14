#pragma once

#include "lvgl.h"

/* Each returns its screen object; called once from ui_init() with the lock held. */
lv_obj_t *screen_splash_create(void);
lv_obj_t *screen_diag_create(void);
lv_obj_t *screen_provisioning_create(void);
lv_obj_t *screen_wifi_create(void);
lv_obj_t *screen_connecting_create(void);
lv_obj_t *screen_pairing_create(void);

/** Marks a label whose text ui_get_texts() must not reveal (the provisioning proof-of-possession). */
#define UI_FLAG_SECRET  LV_OBJ_FLAG_USER_1

/** Screen object with the theme background and no scrolling. */
lv_obj_t *ui_screen_base_create(void);

/** Label with a fixed width at (UI_PAD, y); long text wraps or, with one_line, ends in dots. */
lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t y, bool one_line);

/** Reduces text to printable ASCII and updates the label only when it changed. Caller holds the ui lock. */
void ui_label_set(lv_obj_t *label, const char *text);
