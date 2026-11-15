#include "ui_priv.h"

#include "text.h"
#include "ui.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/*
 * Step 0 (02): the QR code at 4 px per module. The provisioning payload of
 * about 75 bytes needs version 5 (37 modules) at lv_qrcode's ECC level M;
 * the cream square adds the 4-module quiet zone.
 */
#define QR_PX       4
#define QR_MODULES  37
#define QR_QUIET    (4 * QR_PX)
#define QR_SIZE     (QR_MODULES * QR_PX)
#define QR_BOX      (QR_SIZE + 2 * QR_QUIET)
/* Tighter margins than elsewhere: "your phone" in title needs 125 px beside the 180 px code. */
#define QR_X        4
#define QR_Y        24
#define COL_X       (QR_X + QR_BOX + 6)
#define COL_W       (LV_HOR_RES - COL_X - 2)
#define COL_Y       30
#define COL_GAP     8
#define PAYLOAD_SIZE 160

/* Step 1 (02b): name and code to type by hand. */
#define WELL_H      34
#define WELL_PAD_H  10
#define WELL_PAD_V  8

static lv_obj_t *s_step[2];
static lv_obj_t *s_qr;
static lv_obj_t *s_qr_note;
static lv_obj_t *s_service;
static lv_obj_t *s_pop;
static lv_obj_t *s_note;

/* Guarded by the ui lock. */
static uint8_t s_cur;
static char s_payload[PAYLOAD_SIZE];

static lv_obj_t *code_well(lv_obj_t *parent, int32_t y, lv_color_t color)
{
    lv_obj_t *well = ui_box(parent, UI_PAD, y, UI_WIDTH_SURFACE, WELL_H);
    lv_obj_add_style(well, &ui_style_well, 0);
    return ui_text(well, UI_FONT_MONO, color, WELL_PAD_H, WELL_PAD_V, UI_WIDTH_SURFACE - 2 * WELL_PAD_H, true);
}

lv_obj_t *screen_provisioning_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *qr_step = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    ui_rect(qr_step, QR_X, QR_Y, QR_BOX, QR_BOX, UI_C_CREAM, 0);
    s_qr = lv_qrcode_create(qr_step);
    lv_qrcode_set_size(s_qr, QR_SIZE);
    lv_qrcode_set_dark_color(s_qr, UI_C_INK);
    lv_qrcode_set_light_color(s_qr, UI_C_CREAM);
    lv_obj_set_pos(s_qr, QR_X + QR_QUIET, QR_Y + QR_QUIET);
    /* The column beside the code is narrow: its texts wrap, so they stack instead of sitting at fixed rows. */
    lv_obj_t *col = ui_box(qr_step, COL_X, COL_Y, COL_W, QR_Y + QR_BOX - COL_Y);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, COL_GAP, 0);
    lv_obj_t *scan = ui_text(col, UI_FONT_TITLE, UI_C_CREAM, 0, 0, COL_W, false);
    lv_label_set_text_static(scan, "Scan in the ESP app");
    lv_obj_t *how = ui_text(col, UI_FONT_SMALL, UI_C_FOG, 0, 0, COL_W, false);
    lv_label_set_text_static(how, "Use Espressif's ESP BLE Provisioning app; the camera app only shows text.");
    s_qr_note = ui_text(col, UI_FONT_SMALL, UI_C_SLATE, 0, 0, COL_W, false);
    s_step[0] = qr_step;

    lv_obj_t *code_step = ui_box(scr, 0, 0, LV_HOR_RES, LV_VER_RES);
    lv_obj_t *head = ui_text(code_step, UI_FONT_BODY, UI_C_CREAM, UI_TEXT_X, 28, UI_WIDTH_TEXT, true);
    lv_label_set_text_static(head, "Or set it up by hand");
    lv_obj_t *pick = ui_text(code_step, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, 54, UI_WIDTH_TEXT, true);
    lv_label_set_text_static(pick, "In ESP BLE Provisioning, pick this device");
    s_service = code_well(code_step, 74, UI_C_SKY);
    lv_obj_t *type = ui_text(code_step, UI_FONT_SMALL, UI_C_FOG, UI_TEXT_X, 116, UI_WIDTH_TEXT, true);
    lv_label_set_text_static(type, "and type this code when it asks");
    s_pop = code_well(code_step, 136, UI_C_CREAM);
    lv_obj_set_style_text_letter_space(s_pop, 2, 0);
    lv_obj_add_flag(s_pop, UI_FLAG_SECRET);
    s_note = ui_text(code_step, UI_FONT_SMALL, UI_C_SLATE, UI_TEXT_X, 178, UI_WIDTH_TEXT, false);
    s_step[1] = code_step;
    /* The QR code carries the proof-of-possession; ui_get_texts() reports neither. */
    lv_obj_add_flag(s_qr, UI_FLAG_NO_TEXT);

    s_cur = 1;
    ui_provisioning_set_step(0);
    return scr;
}

static void apply_step(uint8_t step)
{
    static const char *const TITLES[2] = {"Wi-Fi setup 1/2", "Wi-Fi setup 2/2"};
    static const ui_tabs_t TABS[2] = {
        {.tab = {[UI_TAB_ACTION] = {.state = UI_TAB_CLICK_AND_HOLD, .label = "No camera", .hold = "Settings"}}},
        {.tab = {[UI_TAB_REJECT] = {.state = UI_TAB_CLICK, .label = "Back"},
                 [UI_TAB_ACTION] = {.state = UI_TAB_CLICK_AND_HOLD, .label = "QR code", .hold = "Settings"}}},
    };
    step = step > 0 ? 1 : 0;
    if (step == s_cur) {
        return;
    }
    s_cur = step;
    ui_set_hidden(s_step[0], step != 0);
    ui_set_hidden(s_step[1], step != 1);
    ui_screen_set_title(UI_SCREEN_PROVISIONING, TITLES[step]);
    ui_screen_set_tabs(UI_SCREEN_PROVISIONING, &TABS[step]);
}

/* Groups the code in fours, as the mockup does, so it is easier to type. */
static void group_code(char *dst, size_t cap, const char *pop)
{
    size_t n = 0;
    for (size_t i = 0; pop[i] != '\0' && n + 2 < cap; i++) {
        if (i > 0 && i % 4 == 0) {
            dst[n++] = ' ';
        }
        dst[n++] = pop[i];
    }
    dst[n] = '\0';
}

static void apply(const char *service_name, const char *pop, const char *status)
{
    char payload[PAYLOAD_SIZE];
    char clean_name[48];
    char clean_pop[48];
    text_sanitize_ascii(clean_name, sizeof(clean_name), service_name != NULL ? service_name : "");
    text_sanitize_ascii(clean_pop, sizeof(clean_pop), pop != NULL ? pop : "");
    /* The provisioning payload of the Espressif phone apps; name and pop are plain ASCII, so no JSON escaping. */
    snprintf(payload, sizeof(payload), "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}",
             clean_name, clean_pop);
    if (strcmp(payload, s_payload) != 0) {
        strlcpy(s_payload, payload, sizeof(s_payload));
        lv_qrcode_update(s_qr, s_payload, (uint32_t)strlen(s_payload));
    }
    char grouped[64];
    group_code(grouped, sizeof(grouped), clean_pop);
    ui_label_set(s_service, clean_name);
    ui_label_set(s_pop, grouped);
    /* A status (an error of the attempt) takes the place of the closing note, in amber. */
    const bool has_status = status != NULL && status[0] != '\0';
    lv_obj_set_style_text_color(s_qr_note, has_status ? UI_C_AMBER : UI_C_SLATE, 0);
    lv_obj_set_style_text_color(s_note, has_status ? UI_C_AMBER : UI_C_SLATE, 0);
    ui_label_set(s_qr_note, has_status ? status : "Bluetooth is on while this shows.");
    ui_label_set(s_note, has_status ? status : "Your old network stays until a new one works.");
}

void ui_provisioning_set(const char *service_name, const char *pop, const char *status)
{
    if (!ui_lock()) {
        return;
    }
    if (!ui_demo_active_locked()) {
        apply(service_name, pop, status);
    }
    ui_unlock();
}

void ui_provisioning_set_step(uint8_t step)
{
    if (!ui_lock()) {
        return;
    }
    apply_step(step);
    ui_unlock();
}

uint8_t ui_provisioning_step(void)
{
    uint8_t step = 0;
    if (ui_lock()) {
        step = s_cur;
        ui_unlock();
    }
    return step;
}

void screen_provisioning_demo(int variant)
{
    if (variant < 0) {
        apply_step(0);
        return;
    }
    apply("DEVOTCHI-3F2A", "7K2M9QXD", "");
    apply_step((uint8_t)variant);
}
