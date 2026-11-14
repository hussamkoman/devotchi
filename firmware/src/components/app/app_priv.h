#pragma once

#include "esp_err.h"
#include "storage.h"

#include <stdbool.h>

/* Diag screen lines owned by the app; 0..3 are filled by fill_diag_screen(). */
#define APP_DIAG_LINE_BATT  4
#define APP_DIAG_LINE_BTN   5

esp_err_t app_cmds_register(void);

/**
 * Tells the app task that a setting was written and waits until it applied it;
 * false when the queue is full or the app task did not get to it in time.
 * One caller at a time (the console task), never the app task itself.
 */
bool app_setting_changed(storage_setting_t id);

/** Commands for buttons, feedback, battery and settings. */
esp_err_t app_cmds_io_register(void);
