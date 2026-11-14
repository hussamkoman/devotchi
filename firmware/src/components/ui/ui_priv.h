#pragma once

#include "lvgl.h"

/* Each returns its screen object; called once from ui_init() with the lock held. */
lv_obj_t *screen_splash_create(void);
lv_obj_t *screen_diag_create(void);

/** Screen object with the theme background and no scrolling. */
lv_obj_t *ui_screen_base_create(void);
