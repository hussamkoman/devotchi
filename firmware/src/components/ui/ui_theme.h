#pragma once

#include "lvgl.h"

#include <stddef.h>

/* Core palette (design-system 2.1). */
#define UI_C_INK           lv_color_hex(0x0C0E14)
#define UI_C_SURFACE       lv_color_hex(0x161A22)
#define UI_C_SURFACE2      lv_color_hex(0x232936)
#define UI_C_LINE          lv_color_hex(0x343C4B)
#define UI_C_CREAM         lv_color_hex(0xF3EEE3)
#define UI_C_FOG           lv_color_hex(0xA7AEBB)
/** Decoration and inactive things only; text a person needs is fog or cream. */
#define UI_C_SLATE         lv_color_hex(0x7C8597)
#define UI_C_SUB_ON_CREAM  lv_color_hex(0x4B5160)

/* Semantic colours (design-system 2.2); each is always paired with a word or a shape. */
#define UI_C_MINT          lv_color_hex(0x5FD38D)
#define UI_C_AMBER         lv_color_hex(0xFFC53D)
#define UI_C_CORAL         lv_color_hex(0xFF6B5E)
#define UI_C_SKY           lv_color_hex(0x6CC4FF)
#define UI_C_VIOLET        lv_color_hex(0xB592FF)
#define UI_C_MANGO         lv_color_hex(0xFFB84D)

/** Slate as an LVGL recolour command ("#RRGGBB text#"), for a label inside a line of other text. */
#define UI_RECOLOR_SLATE   "#7C8597"

/* Night palette of the clock face (design-system 2.4). */
#define UI_C_NIGHT_BG      lv_color_hex(0x000000)
#define UI_C_EMBER         lv_color_hex(0x9A4A26)
#define UI_C_EMBER_DIM     lv_color_hex(0x6E3620)

/* The pet's body colour comes from its pose (pet_pose.c); these draw the features on it. */
#define UI_C_PET_INK       lv_color_hex(0x1A1D26)
#define UI_C_PET_EYE       UI_C_CREAM
#define UI_C_PET_DROP      lv_color_hex(0xB3E5FC)
#define UI_C_PET_SPARKLE   UI_C_MANGO
#define UI_C_PET_ZZZ       UI_C_FOG
#define UI_C_PET_DOTS      UI_C_CREAM

LV_FONT_DECLARE(ui_font_display64)
LV_FONT_DECLARE(ui_font_title24)
LV_FONT_DECLARE(ui_font_body18)
LV_FONT_DECLARE(ui_font_small14)
LV_FONT_DECLARE(ui_font_mono14)
LV_FONT_DECLARE(ui_font_icons)

/*
 * Text styles (design-system 3). DISPLAY and TITLE hold only the glyphs the
 * firmware's own strings need (digits and pairing letters; ASCII): text that
 * comes from outside (project, device name, question, plan) uses BODY, SMALL
 * or MONO, which carry the whole device charset.
 */
#define UI_FONT_DISPLAY    (&ui_font_display64)
#define UI_FONT_TITLE      (&ui_font_title24)
#define UI_FONT_BODY       (&ui_font_body18)
#define UI_FONT_SMALL      (&ui_font_small14)
#define UI_FONT_MONO       (&ui_font_mono14)
/** Icons of design-system 7 (ui_icons.h); 14 px, the hold glyphs 16 px. */
#define UI_FONT_ICONS      (&ui_font_icons)
/** caps: small at 9 % letter spacing (14 px * 0.09), upper-cased by ui_label_set_caps(). */
#define UI_CAPS_LETTER_SPACE  1

/* Layout (design-system 4). */
#define UI_UNIT            4
/** Outer margin of surfaces (cards, wells, rows). */
#define UI_PAD             8
/** Outer margin of bare text, so it shares a left edge with text inside a surface. */
#define UI_TEXT_X          12
#define UI_STATUS_BAR_H    20
#define UI_CONTENT_Y       20
#define UI_CONTENT_BOTTOM  215
#define UI_TABS_Y          218
#define UI_TAB_H           22
/** First y below the status bar for screen content. */
#define UI_TOP             24
#define UI_WIDTH_SURFACE   (LV_HOR_RES - 2 * UI_PAD)
#define UI_WIDTH_TEXT      (LV_HOR_RES - 2 * UI_TEXT_X)

#define UI_R_TAG           4
#define UI_R_ROW           8
#define UI_R_WELL          8
#define UI_R_TOAST         12
#define UI_R_CHIP          11
#define UI_R_TAB           10

/* Motion (design-system 8). */
#define UI_MS_QUICK        120
#define UI_MS_MOVE         200
#define UI_MS_MOOD         300

/* Shared styles, initialised once by ui_theme_init(). */
/** Code well and card surface: surface colour, row radius. */
extern lv_style_t ui_style_well;
/** Button tab body: surface-2 with rounded top corners. */
extern lv_style_t ui_style_tab;
/** Focused list row: cream fill. */
extern lv_style_t ui_style_focus;
/** Pill chip: full radius; the caller sets the colour. */
extern lv_style_t ui_style_chip;
/** Toast and receipt strip: surface-2 with a line border. */
extern lv_style_t ui_style_toast;
/** caps text: small font, letter spacing, fog. */
extern lv_style_t ui_style_caps;

/** Initialises the shared styles and the default text font of the display; once, with the ui lock held. */
void ui_theme_init(lv_display_t *disp);
