#include <stddef.h>
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

#define LCD_BACKLIGHT_GPIO GPIO_NUM_26
#define BUFFER_SIZE (LCD_WIDTH * LCD_TRANSFER_LINES)

static esp_lcd_panel_handle_t panel_handle = NULL;
static const char *TAG = "display";

// We write up to LCD_TRANSFER_LINES at time.
// at 480 * 40, we have 19,200 entries at 2 bytes each so, 38,400 bytes
// 38.4 KB, which is fine i think
static display_color_t buffer[BUFFER_SIZE];


display_color_t rgb656(uint8_t r, uint8_t g, uint8_t b) {
  return ((r >> 3) << 11 ) | ((g >> 2) << 5) | (b >> 3);
}

void display_init(void) {
  // reset and pull backlight to high
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
}

void display_sleep(void) {
  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 0));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_sleep(panel_handle, true));
  ESP_LOGI(TAG, "Display is now asleep");
}

void display_wake(void) {
  ESP_ERROR_CHECK(esp_lcd_panel_disp_sleep(panel_handle, false));
  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 1));
  ESP_LOGI(TAG, "Display is now awake");
}

void display_clear(display_color_t color) {
  // Fill buffer, with color, then just iterate until height is done times
  for (size_t i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = color;
  }

  for (size_t i = 0; i < LCD_HEIGHT; i += LCD_TRANSFER_LINES) {
    ESP_ERROR_CHECK(
      esp_lcd_panel_draw_bitmap(panel_handle, 0, i, LCD_WIDTH, i + LCD_TRANSFER_LINES, buffer)
    );
  }
};


