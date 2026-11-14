#pragma once

#include "esp_err.h"

/* Diag screen lines owned by the app; 0..3 are filled by fill_diag_screen(). */
#define APP_DIAG_LINE_BATT  4
#define APP_DIAG_LINE_BTN   5

esp_err_t app_cmds_register(void);

/** Commands for buttons, feedback, battery and settings. */
esp_err_t app_cmds_io_register(void);
