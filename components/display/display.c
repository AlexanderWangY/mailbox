#include <stdint.h>
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_types.h"

static esp_lcd_panel_handle_t panel_handle = NULL;
static const char *TAG = "display";
static uint16_t color_buffer[LCD_WIDTH * LCD_TRANSFER_LINES];

#define LCD_RED_RGB565 0xF800
#define LCD_GREEN_RGB565 0x07E0
#define LCD_BLUE_RGB565 0x001F

#define LCD_BACKLIGHT_GPIO GPIO_NUM_26

void display_init(void) {
  // reseting and pulling backlight to high
  ESP_ERROR_CHECK(gpio_reset_pin(LCD_BACKLIGHT_GPIO));
  ESP_ERROR_CHECK(gpio_set_direction(LCD_BACKLIGHT_GPIO, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 1));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_cfg = {
    .dc_gpio_num = GPIO_NUM_14,
    .cs_gpio_num = GPIO_NUM_27,
    .pclk_hz = 20 * 1000 * 1000,
    .spi_mode = 0,
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
    (esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &io_cfg, &io_handle
  ));

  ESP_LOGI(TAG, "Creating LCD panel.");
  esp_lcd_panel_dev_config_t panel_cfg = {
    .reset_gpio_num = GPIO_NUM_32,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
  };

  ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_cfg, &panel_handle));

  ESP_LOGI(TAG, "Resetting display panel.");
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_LOGI(TAG, "Initializing display panel.");
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  // holy shit this invert color thing was annoying to figure out I needed to do this.
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  ESP_LOGI(TAG, "Setting landscape orientation (480x320).");
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
  ESP_LOGI(TAG, "Display initialized.");


  // Do a simpel RGB screen test
  // TODO: remove this since this is just a test
  for (int row = 0; row < LCD_TRANSFER_LINES; ++row) {
    for (int x = 0; x < LCD_WIDTH; ++x) {
      uint16_t color = x < LCD_WIDTH / 3 ? LCD_RED_RGB565
        : (x < (2 * LCD_WIDTH) / 3 ? LCD_BLUE_RGB565 : LCD_GREEN_RGB565);
      color_buffer[row * LCD_WIDTH + x] = color;
    }
  }

  for (int y = 0; y < LCD_HEIGHT; y += LCD_TRANSFER_LINES) {
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
      panel_handle,
      0,
      y,
      LCD_WIDTH,
      y + LCD_TRANSFER_LINES,
      color_buffer
    ));
  }
}
