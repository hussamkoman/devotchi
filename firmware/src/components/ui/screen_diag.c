#include "ui_priv.h"

#include "text.h"
#include "ui.h"
#include "ui_theme.h"

#include <string.h>

#define DIAG_LINE_HEIGHT 28
#define DIAG_FIRST_LINE_Y 56

static lv_obj_t *s_lines[UI_DIAG_LINES];

lv_obj_t *screen_diag_create(void)
{
    lv_obj_t *scr = ui_screen_base_create();

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_ACCENT, 0);
    lv_label_set_text_static(title, "Devotchi diag");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, UI_PAD, UI_PAD);

    for (size_t i = 0; i < UI_DIAG_LINES; i++) {
        lv_obj_t *line = lv_label_create(scr);
        lv_obj_set_style_text_font(line, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(line, UI_COLOR_TEXT, 0);
        lv_obj_set_size(line, LV_HOR_RES - 2 * UI_PAD, lv_font_get_line_height(UI_FONT_SMALL));
        lv_label_set_long_mode(line, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text_static(line, "");
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, UI_PAD, DIAG_FIRST_LINE_Y + (int32_t)i * DIAG_LINE_HEIGHT);
        s_lines[i] = line;
    }
    return scr;
}

void ui_diag_set_line(size_t line, const char *text)
{
    if (line >= UI_DIAG_LINES) {
        return;
    }
    char clean[UI_TEXT_SIZE];
    text_sanitize_ascii(clean, sizeof(clean), text);
    if (!ui_lock()) {
        return;
    }
    if (strcmp(lv_label_get_text(s_lines[line]), clean) != 0) {
        lv_label_set_text(s_lines[line], clean);
    }
    ui_unlock();
}
