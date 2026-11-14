#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Brings up the backlight PWM, the ST7789 panel and the LVGL display.
 * The backlight stays off until the first complete frame reached the panel.
 */
esp_err_t display_init(void);

/**
 * Takes the recursive lock that guards LVGL and every esp_lcd panel call.
 * The ui component exposes it as ui_lock(); returns false on timeout.
 */
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);

/** Sets the backlight level used whenever the panel is awake. */
void display_set_brightness(uint8_t percent);

/** Puts the panel into or out of sleep (SLPIN/SLPOUT) and switches the backlight with it. */
esp_err_t display_sleep(bool sleep);

/**
 * Renders pending changes and copies the panel content (RGB565 little-endian,
 * row-major) into the snapshot buffer. The buffer stays valid and unchanged
 * until the next call; one caller at a time (the console task).
 */
esp_err_t display_snapshot(const uint8_t **buf, size_t *len, uint32_t *crc32);

/** Panel transfers that failed or did not complete in time since boot. */
uint32_t display_flush_errors(void);

/** Areas flushed to the panel since boot; a change means a frame was rendered. */
uint32_t display_flush_count(void);
