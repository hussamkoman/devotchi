#pragma once

#include "lvgl.h"

#define UI_COLOR_BG          lv_color_hex(0x000000)
#define UI_COLOR_TEXT        lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_DIM    lv_color_hex(0x9AA0A6)
#define UI_COLOR_ACCENT      lv_color_hex(0x4FC3F7)
#define UI_COLOR_OK          lv_color_hex(0x66BB6A)
#define UI_COLOR_WARN        lv_color_hex(0xFFB300)
#define UI_COLOR_DANGER      lv_color_hex(0xEF5350)
#define UI_COLOR_RISK_LOW    UI_COLOR_OK
#define UI_COLOR_RISK_MED    UI_COLOR_WARN
#define UI_COLOR_RISK_HIGH   UI_COLOR_DANGER
/** Text on a coloured chip or banner. */
#define UI_COLOR_ON_CHIP     lv_color_hex(0x000000)
#define UI_COLOR_BAR_BG      lv_color_hex(0x15181C)
#define UI_COLOR_DIVIDER     lv_color_hex(0x30343A)
#define UI_COLOR_TOAST_BG    lv_color_hex(0x2C3036)
#define UI_COLOR_INACTIVE    lv_color_hex(0x4A4F55)
/* The pet's body colour comes from its pose (pet_pose.c); these draw the features on it. */
#define UI_COLOR_PET_INK     lv_color_hex(0x1B1E22)
#define UI_COLOR_PET_EYE     lv_color_hex(0xFFFFFF)
#define UI_COLOR_PET_DROP    lv_color_hex(0xB3E5FC)
#define UI_COLOR_PET_SPARKLE UI_COLOR_WARN
#define UI_COLOR_PET_ZZZ     UI_COLOR_TEXT_DIM
#define UI_COLOR_PET_DOTS    UI_COLOR_TEXT

#define UI_FONT_SMALL        (&lv_font_montserrat_14)
#define UI_FONT_BODY         (&lv_font_montserrat_18)
#define UI_FONT_TITLE        (&lv_font_montserrat_28)
/* unscii_16 is 16 px wide per glyph, so 38 columns would need 608 px. */
#define UI_FONT_MONO         (&ui_font_mono_13)

#define UI_PAD               8
#define UI_STATUS_BAR_H      20
/** First y below the status bar for screen content. */
#define UI_TOP               (UI_STATUS_BAR_H + 4)

LV_FONT_DECLARE(ui_font_mono_13)
