#pragma once

#include <cstdint>

namespace devotchi::display {

/** Drives the ST7789 TFT over SPI. Synchronous; not thread-safe. */
class Display {
public:
    void init();

    void clear(uint16_t color = 0x0000);
    void fill_rect(int x, int y, int w, int h, uint16_t color);
    void draw_bitmap(int x, int y, int w, int h, const uint16_t* buffer);
    void set_brightness(uint8_t pct);
    void sleep();
    void wake();

private:
    void* panel_io_ = nullptr;       // esp_lcd_panel_io_handle_t
    void* panel_    = nullptr;       // esp_lcd_panel_handle_t
    uint8_t brightness_pct_ = 0;
};

}  // namespace devotchi::display
