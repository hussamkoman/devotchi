#include "ui_priv.h"

#include "pair_code.h"
#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* Layout of the mockup (06): the code in two groups of three, the expiry beside it. */
#define CODE_X       14
#define CODE_Y1      22
#define CODE_Y2      84
#define CODE_W       190
#define SIDE_X       214
#define SIDE_W       96
#define EXPIRES_Y    34
#define LEFT_Y       52
#define ERROR_Y      88
#define ERROR_LINES  3
#define WELL_Y       153
#define WELL_H       34
#define WELL_PAD_H   10
#define WELL_PAD_V   8
#define SERVER_Y     190
#define SERVER_W     52
#define BAR_Y        212
#define HOST_COLS    28

static lv_obj_t *s_code[2];
static lv_obj_t *s_left;
static lv_obj_t *s_error;
static lv_obj_t *s_command;
static lv_obj_t *s_host;
static w_countdown_t *s_bar;

lv_obj_t *screen_pairing_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    for (int i = 0; i < 2; i++) {
        s_code[i] = ui_text(scr, UI_FONT_DISPLAY, UI_C_CREAM, CODE_X, i == 0 ? CODE_Y1 : CODE_Y2, CODE_W, true);
        lv_obj_set_style_text_letter_space(s_code[i], 3, 0);
    }
    lv_obj_t *expires = ui_text(scr, UI_FONT_SMALL, UI_C_SLATE, SIDE_X, EXPIRES_Y, SIDE_W, true);
    lv_obj_add_style(expires, &ui_style_caps, 0);
    lv_obj_set_style_text_color(expires, UI_C_SLATE, 0);
    lv_label_set_text_static(expires, "EXPIRES");
    s_left = ui_text(scr, UI_FONT_TITLE, UI_C_CREAM, SIDE_X, LEFT_Y, SIDE_W, true);
    s_error = ui_text(scr, UI_FONT_SMALL, UI_C_CORAL, SIDE_X, ERROR_Y, SIDE_W, false);
    lv_obj_set_height(s_error, ERROR_LINES * lv_font_get_line_height(UI_FONT_SMALL));
    lv_label_set_long_mode(s_error, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *well = ui_box(scr, UI_PAD, WELL_Y, UI_WIDTH_SURFACE, WELL_H);
    lv_obj_add_style(well, &ui_style_well, 0);
    s_command = ui_text(well, UI_FONT_MONO, UI_C_CREAM, WELL_PAD_H, WELL_PAD_V, UI_WIDTH_SURFACE - 2 * WELL_PAD_H,
                        true);

    lv_obj_t *server = ui_text(scr, UI_FONT_SMALL, UI_C_SLATE, UI_TEXT_X, SERVER_Y, SERVER_W, true);
    lv_label_set_text_static(server, "Server");
    s_host = ui_text(scr, UI_FONT_MONO, UI_C_FOG, UI_TEXT_X + SERVER_W, SERVER_Y, UI_WIDTH_TEXT - SERVER_W, true);
    s_bar = w_countdown_create(scr, BAR_Y, false);

    ui_screen_set_title(UI_SCREEN_PAIRING, "Pairing");
    const ui_tabs_t tabs = {
        .tab = {[UI_TAB_ACTION] = {.state = UI_TAB_CLICK_AND_HOLD, .label = "New code", .hold = "Settings"}},
    };
    ui_screen_set_tabs(UI_SCREEN_PAIRING, &tabs);
    return scr;
}

static void apply(const char *code, const char *host, const char *error, uint32_t remaining_s, uint32_t total_s)
{
    char display[PAIR_CODE_DISPLAY_SIZE];
    char half[2][8] = {"---", "---"};
    char command[48] = "";
    char left[16] = "";
    if (pair_code_format(code, display)) {
        /* display is "ABC DEF". */
        snprintf(half[0], sizeof(half[0]), "%.3s", display);
        snprintf(half[1], sizeof(half[1]), "%.3s", display + 4);
        snprintf(command, sizeof(command), "$ devotchi pair %s", code);
        snprintf(left, sizeof(left), "%lu:%02lu", (unsigned long)(remaining_s / 60), (unsigned long)(remaining_s % 60));
    } else {
        remaining_s = 0;
    }
    if (total_s == 0) {
        total_s = 1;
    }
    if (remaining_s > total_s) {
        remaining_s = total_s;
    }
    char name[UI_TEXT_SIZE];
    ui_host_text(name, sizeof(name), host, HOST_COLS);

    ui_label_set(s_code[0], half[0]);
    ui_label_set(s_code[1], half[1]);
    ui_label_set(s_left, left);
    ui_label_set(s_error, error);
    ui_label_set(s_command, command);
    ui_label_set(s_host, name);
    w_countdown_set(s_bar, (uint16_t)(remaining_s * 1000 / total_s), (int32_t)remaining_s, UI_C_CREAM);
}

void ui_pairing_set(const char *code, const char *host, const char *error, uint32_t remaining_s, uint32_t total_s)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active_locked()) {
        apply(code, host, error, remaining_s, total_s);
    }
    ui_unlock();
}

void screen_pairing_demo(void)
{
    apply("K7M2QX", "ws://devotchi.local:8080", "", 272, 360);
}
