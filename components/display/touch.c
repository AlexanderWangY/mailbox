#include "freertos/FreeRTOS.h"
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft6x36.h"
#include "touch.h"

static esp_lcd_panel_io_handle_t touch_io_handle;
static esp_lcd_touch_handle_t touch_handle;

void touch_init(void) {
  const gpio_config_t reset_gpio_config = {
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = (1ULL << GPIO_NUM_13),
  };
  ESP_ERROR_CHECK(gpio_config(&reset_gpio_config));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 0));
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 1));
  // FT6336U can take up to 300 ms to become ready after reset.
  vTaskDelay(pdMS_TO_TICKS(300));

  esp_lcd_panel_io_i2c_config_t io_config =
    ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();
  io_config.dev_addr = 0x38;
  io_config.scl_speed_hz = 100'000;

  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
    board_i2c_bus_handle(), &io_config, &touch_io_handle
  ));

  esp_lcd_touch_config_t touch_config = {
    .x_max = LCD_WIDTH,
    .y_max = LCD_HEIGHT,
    // Reset was pulsed above with the datasheet-required startup delay.
    .rst_gpio_num = GPIO_NUM_NC,
    .int_gpio_num = GPIO_NUM_33,
    .levels = {
      .reset = 0,
      .interrupt = 0,
    },
    .flags = {
      .swap_xy = true,
    },
  };

  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft6x36(
    touch_io_handle, &touch_config, &touch_handle
  ));
}

void touch_read(void) {}
