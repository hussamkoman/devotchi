#include "ui_priv.h"

#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>

#define STATUS_Y     190
#define DOTS_Y       226
#define DOT_SIZE     6
#define DOT_GAP      8
#define ABOUT_ROW_H  16

static const char *const ABOUT_LABELS[UI_ABOUT_COUNT] = {
    [UI_ABOUT_NAME] = "Name",       [UI_ABOUT_FW] = "Firmware",  [UI_ABOUT_DEVICE_ID] = "Device",
    [UI_ABOUT_UPTIME] = "Uptime",   [UI_ABOUT_RESET] = "Reset",  [UI_ABOUT_SSID] = "WiFi",
    [UI_ABOUT_IP] = "IP",           [UI_ABOUT_RSSI] = "RSSI",    [UI_ABOUT_SERVER] = "Server",
    [UI_ABOUT_LINK] = "Link",       [UI_ABOUT_ERROR] = "Error",  [UI_ABOUT_NVS] = "NVS",
};

static lv_obj_t *s_project;
static lv_obj_t *s_status;
static lv_obj_t *s_about;
static lv_obj_t *s_about_rows[UI_ABOUT_COUNT];
static lv_obj_t *s_dots[UI_PET_PAGE_COUNT];

/* Guarded by the ui lock. */
static ui_pet_page_t s_page = UI_PET_PAGE_PET;
static bool s_shown;

static void apply_page(void)
{
    const bool pet = s_page == UI_PET_PAGE_PET;
    if (pet) {
        lv_obj_add_flag(s_about, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_project, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_about, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_project, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < UI_PET_PAGE_COUNT; i++) {
        lv_obj_set_style_bg_color(s_dots[i], i == (int)s_page ? UI_COLOR_TEXT : UI_COLOR_INACTIVE, 0);
    }
    pet_view_set_visible(s_shown && pet);
}

lv_obj_t *screen_pet_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();
    s_project = ui_label_create(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, UI_TOP, true);
    lv_obj_set_style_text_align(s_project, LV_TEXT_ALIGN_CENTER, 0);
    pet_view_create(scr);
    s_status = ui_label_create(scr, UI_FONT_BODY, UI_COLOR_TEXT, STATUS_Y, true);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);

    s_about = lv_obj_create(scr);
    lv_obj_remove_style_all(s_about);
    lv_obj_set_size(s_about, LV_HOR_RES, ABOUT_ROW_H * UI_ABOUT_COUNT);
    lv_obj_set_pos(s_about, 0, UI_TOP);
    lv_obj_remove_flag(s_about, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < UI_ABOUT_COUNT; i++) {
        s_about_rows[i] = ui_label_create(s_about, UI_FONT_SMALL, UI_COLOR_TEXT, i * ABOUT_ROW_H, true);
    }

    const int32_t x0 = (LV_HOR_RES - UI_PET_PAGE_COUNT * DOT_SIZE - (UI_PET_PAGE_COUNT - 1) * DOT_GAP) / 2;
    for (int i = 0; i < UI_PET_PAGE_COUNT; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], DOT_SIZE, DOT_SIZE);
        lv_obj_set_pos(s_dots[i], x0 + i * (DOT_SIZE + DOT_GAP), DOTS_Y);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    apply_page();
    return scr;
}

void screen_pet_set_shown(bool shown)
{
    s_shown = shown;
    apply_page();
}

void ui_pet_set(const char *project, proto_mood_t mood, const char *status)
{
    if (!ui_lock()) {
        return;
    }
    ui_label_set(s_project, project);
    pet_view_set_mood(mood);
    ui_label_set(s_status, status);
    ui_unlock();
}

void ui_pet_page_set(ui_pet_page_t page)
{
    if (page >= UI_PET_PAGE_COUNT || !ui_lock()) {
        return;
    }
    if (page != s_page) {
        s_page = page;
        apply_page();
    }
    ui_unlock();
}

void ui_about_set(const char *const values[UI_ABOUT_COUNT])
{
    char row[UI_TEXT_SIZE];
    if (!ui_lock()) {
        return;
    }
    for (int i = 0; i < UI_ABOUT_COUNT; i++) {
        snprintf(row, sizeof(row), "%s: %s", ABOUT_LABELS[i], values[i] != NULL ? values[i] : "");
        ui_label_set(s_about_rows[i], row);
    }
    ui_unlock();
}

const char *ui_pet_reaction_name(ui_pet_reaction_t r)
{
    switch (r) {
    case UI_PET_REACT_NONE:
        return "none";
    case UI_PET_REACT_PAT:
        return "pat";
    case UI_PET_REACT_POKE:
        return "poke";
    case UI_PET_REACT_DIZZY:
        return "dizzy";
    }
    return "none";
}

void ui_pet_react(ui_pet_reaction_t r)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_react(r);
    ui_unlock();
}

void ui_pet_set_running(bool running)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_set_powered(running);
    ui_unlock();
}

void ui_pet_freeze(bool frozen)
{
    if (!ui_lock()) {
        return;
    }
    pet_view_set_frozen(frozen);
    ui_unlock();
}

void ui_pet_get_state(ui_pet_state_t *out)
{
    *out = (ui_pet_state_t){0};
    if (!ui_lock()) {
        return;
    }
    pet_view_get_state(out);
    ui_unlock();
}
