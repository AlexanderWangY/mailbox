#include "freertos/FreeRTOS.h"
#include "app.h"
#include "esp_attr.h"
#include "esp_lcd_touch.h"
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft6x36.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"
#include "touch.h"
#include <stdbool.h>
#include <stdint.h>

static esp_lcd_panel_io_handle_t touch_io_handle;
static esp_lcd_touch_handle_t touch_handle;
static TaskHandle_t touch_task_handle;

static void IRAM_ATTR touch_isr(esp_lcd_touch_handle_t tp) {
  (void)tp;
  BaseType_t higher_priority_task_awoken = pdFALSE;

  vTaskNotifyGiveFromISR(touch_task_handle, &higher_priority_task_awoken);

  if (higher_priority_task_awoken) {
    portYIELD_FROM_ISR();
  }
}

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

  BaseType_t task_created = xTaskCreate(
    touch_task, "touch task", 4096, NULL, 3, &touch_task_handle
  );
  ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(
    esp_lcd_touch_register_interrupt_callback(touch_handle, touch_isr)
  );
}

void touch_read(void) {}

void touch_task(void *arg) {
  (void)arg;
  bool was_pressed = false;

  for (;;) {
    if (!was_pressed) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    } else {
      // Keep checking briefly while a finger is down so release is detected
      // even if the controller doesn't generate a separate release edge.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
    }

    if (esp_lcd_touch_read_data(touch_handle) != ESP_OK) {
      continue;
    }

    uint16_t x[2], y[2], strength[2];
    uint8_t point_count = 0;
    bool is_pressed = esp_lcd_touch_get_coordinates(
      touch_handle, x, y, strength, &point_count, 2
    ) && point_count > 0;

    if (is_pressed && !was_pressed) {
      app_event_t event = {
        .type = APP_EVENT_TOUCH_TAP,
      };
      app_create_event(&event);
    }

    was_pressed = is_pressed;
  }
}
