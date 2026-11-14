#pragma once

#include "lvgl.h"

#define UI_COLOR_BG          lv_color_hex(0x000000)
#define UI_COLOR_TEXT        lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_DIM    lv_color_hex(0x9AA0A6)
#define UI_COLOR_ACCENT      lv_color_hex(0x4FC3F7)
#define UI_COLOR_OK          lv_color_hex(0x66BB6A)
#define UI_COLOR_WARN        lv_color_hex(0xFFB300)
#define UI_COLOR_DANGER      lv_color_hex(0xEF5350)

#define UI_FONT_SMALL        (&lv_font_montserrat_14)
#define UI_FONT_BODY         (&lv_font_montserrat_18)
#define UI_FONT_TITLE        (&lv_font_montserrat_28)
#define UI_FONT_MONO         (&lv_font_unscii_16)

#define UI_PAD               8
#define UI_STATUS_BAR_H      20
