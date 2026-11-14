#include "Display.hpp"

#include "Config.hpp"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

namespace devotchi::display {

namespace {
constexpr const char *TAG = "Display";

constexpr ledc_mode_t BL_SPEED_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t BL_TIMER = LEDC_TIMER_1;
constexpr ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_2;
constexpr uint8_t BL_RES_BITS = 10;
constexpr uint32_t BL_DUTY_MAX = (1u << BL_RES_BITS) - 1u;

constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
constexpr int LCD_CMD_BITS = 8;
constexpr int LCD_PARAM_BITS = 8;
} // namespace

void Display::init() {
  ledc_timer_config_t bl_timer = {
      .speed_mode = BL_SPEED_MODE,
      .duty_resolution = static_cast<ledc_timer_bit_t>(BL_RES_BITS),
      .timer_num = BL_TIMER,
      .freq_hz = config::display::BL_PWM_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
      .deconfigure = false,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));

  ledc_channel_config_t bl_ch = {
      .gpio_num = config::pins::DISP_BL,
      .speed_mode = BL_SPEED_MODE,
      .channel = BL_CHANNEL,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = BL_TIMER,
      .duty = 0,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {},
      .deconfigure = false,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&bl_ch));

  spi_bus_config_t buscfg = {
      .mosi_io_num = config::pins::DISP_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = config::pins::DISP_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .data4_io_num = -1,
      .data5_io_num = -1,
      .data6_io_num = -1,
      .data7_io_num = -1,
      .data_io_default_level = false,
      .max_transfer_sz =
          config::display::WIDTH * 40 * static_cast<int>(sizeof(uint16_t)),
      .flags = 0,
      .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
      .intr_flags = 0,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_cfg = {
      .cs_gpio_num = config::pins::DISP_CS,
      .dc_gpio_num = config::pins::DISP_DC,
      .spi_mode = 0,
      .pclk_hz = config::display::SPI_HZ,
      .trans_queue_depth = 10,
      .on_color_trans_done = nullptr,
      .user_ctx = nullptr,
      .lcd_cmd_bits = LCD_CMD_BITS,
      .lcd_param_bits = LCD_PARAM_BITS,
      .cs_ena_pretrans = 0,
      .cs_ena_posttrans = 0,
      .flags = {},
  };
  esp_lcd_panel_io_handle_t io_handle = nullptr;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      static_cast<esp_lcd_spi_bus_handle_t>(LCD_HOST), &io_cfg, &io_handle));
  panel_io_ = io_handle;

  esp_lcd_panel_dev_config_t panel_cfg = {
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
      .bits_per_pixel = 16,
      .reset_gpio_num = config::pins::DISP_RST,
      .vendor_config = nullptr,
      .flags = {},
  };
  esp_lcd_panel_handle_t panel = nullptr;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel));
  panel_ = panel;

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

  set_brightness(config::ui::BRIGHTNESS_DEFAULT);

  ESP_LOGI(TAG,
           "init — ST7789 %dx%d, SPI@%lu Hz on SCLK=%d MOSI=%d CS=%d DC=%d "
           "RST=%d BL=%d",
           config::display::WIDTH, config::display::HEIGHT,
           static_cast<unsigned long>(config::display::SPI_HZ),
           config::pins::DISP_SCLK, config::pins::DISP_MOSI,
           config::pins::DISP_CS, config::pins::DISP_DC, config::pins::DISP_RST,
           config::pins::DISP_BL);
}

void Display::clear(uint16_t color) {
  fill_rect(0, 0, config::display::WIDTH, config::display::HEIGHT, color);
}

void Display::fill_rect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0)
    return;
  if (x < 0 || y < 0)
    return;
  if (x + w > config::display::WIDTH || y + h > config::display::HEIGHT)
    return;

  uint16_t row[config::display::WIDTH];
  for (int i = 0; i < w; ++i) {
    row[i] = color;
  }

  auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
  for (int row_y = y; row_y < y + h; ++row_y) {
    esp_lcd_panel_draw_bitmap(panel, x, row_y, x + w, row_y + 1, row);
  }
}

void Display::draw_bitmap(int x, int y, int w, int h, const uint16_t *buffer) {
  if (w <= 0 || h <= 0)
    return;
  auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
  esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, buffer);
}

void Display::set_brightness(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  brightness_pct_ = pct;
  const uint32_t duty = (static_cast<uint32_t>(pct) * BL_DUTY_MAX) / 100u;
  ledc_set_duty(BL_SPEED_MODE, BL_CHANNEL, duty);
  ledc_update_duty(BL_SPEED_MODE, BL_CHANNEL);
}

void Display::sleep() {
  ledc_set_duty(BL_SPEED_MODE, BL_CHANNEL, 0);
  ledc_update_duty(BL_SPEED_MODE, BL_CHANNEL);
  esp_lcd_panel_disp_on_off(static_cast<esp_lcd_panel_handle_t>(panel_), false);
}

void Display::wake() {
  esp_lcd_panel_disp_on_off(static_cast<esp_lcd_panel_handle_t>(panel_), true);
  set_brightness(brightness_pct_);
}

} // namespace devotchi::display
