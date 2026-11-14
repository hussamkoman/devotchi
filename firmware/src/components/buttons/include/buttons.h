#pragma once

#include "btn_fsm.h"
#include "esp_err.h"

#include <stdint.h>

/** Runs in the esp_timer task; it must only copy the data and post it. */
typedef void (*buttons_cb_t)(btn_id_t id, btn_raw_t raw, uint32_t now_ms);

/**
 * Creates the three GPIO buttons. Must run before anything else installs the
 * GPIO ISR service: the button library installs it itself in power-save mode
 * and fails if it already exists.
 */
esp_err_t buttons_init(buttons_cb_t cb);
