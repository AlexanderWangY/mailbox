#include "board.h"
#include "freertos/FreeRTOS.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_types.h"
#include "esp_lvgl_port.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/ledc_types.h"
#include "include/lvgl/display/lv_display.h"
#include "portmacro.h"
#include "soc/clk_tree_defs.h"
#include <stdint.h>

#define LCD_BACKLIGHT_GPIO GPIO_NUM_26
#define BUFFER_SIZE (LCD_WIDTH * LCD_TRANSFER_LINES)

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static const char *TAG = "display";

static QueueHandle_t display_queue;

static display_state_t state = DISPLAY_NORMAL;

void display_init(void) {
  // reset and pull backlight to high
  ESP_ERROR_CHECK(gpio_reset_pin(LCD_BACKLIGHT_GPIO));
  ESP_ERROR_CHECK(gpio_set_direction(LCD_BACKLIGHT_GPIO, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 1));


  esp_lcd_panel_io_spi_config_t io_cfg = {
    .dc_gpio_num = GPIO_NUM_14,
    .cs_gpio_num = GPIO_NUM_27,
    .pclk_hz = 40 * 1000 * 1000,
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
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
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
  ESP_LOGI(TAG, "Hooking LVGL display initialization.");
  lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
  lvgl_port_display_cfg_t disp_cfg = {
    .io_handle = io_handle,
    .panel_handle = panel_handle,
    .buffer_size = BUFFER_SIZE,
    .double_buffer = true,
    .hres = LCD_WIDTH,
    .vres = LCD_HEIGHT,
    .rotation = {
      .swap_xy = true,
    },
    .flags = {
      .buff_dma = true,
      .swap_bytes = true,
    },
  };
  lvgl_port_add_disp(&disp_cfg);
  ESP_LOGI(TAG, "LVGL initialized.");
  ESP_LOGI(TAG, "Display initialized.");

  ledc_timer_config_t timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .timer_num = LEDC_TIMER_0,
    .duty_resolution = LEDC_TIMER_10_BIT,
    .freq_hz = 20000,
    .clk_cfg = LEDC_AUTO_CLK,
  };

  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t channel = {
    .gpio_num = LCD_BACKLIGHT_GPIO,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = 1023,
    .hpoint = 0,
  };

  ESP_ERROR_CHECK(ledc_channel_config(&channel));
  ESP_LOGI(TAG, "LED PWM configured.");

  display_queue = xQueueCreate(16, sizeof(display_cmd_t));
  ESP_LOGI(TAG, "Display queue initialized.");
}

void display_sleep(void) {
  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 0));
  lvgl_port_stop();
  ESP_ERROR_CHECK(esp_lcd_panel_disp_sleep(panel_handle, true));
  state = DISPLAY_SLEEP;
  ESP_LOGI(TAG, "Display is now asleep");
}

void display_wake(void) {
  ESP_ERROR_CHECK(esp_lcd_panel_disp_sleep(panel_handle, false));
  lvgl_port_resume();

  if (lvgl_port_lock(0)) {
    lv_obj_invalidate(lv_screen_active());
    lvgl_port_unlock();
  }

  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 1));  
  ESP_LOGI(TAG, "Display is now awake");
}

void display_dim(void) {
  uint32_t duty = 1023 * 0.5;
  ESP_ERROR_CHECK(
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty)
  );

  ESP_ERROR_CHECK(
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0)
  );

  state = DISPLAY_DIM;
};

void display_normal(void) {
  uint32_t duty = 1023;
  ESP_ERROR_CHECK(
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty)
  );

  ESP_ERROR_CHECK(
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0)
  );

  state = DISPLAY_NORMAL;
};

display_state_t display_state(void) {
  return state;
}

void display_clear() {
  lvgl_port_lock(0);
  lv_obj_clean(lv_screen_active());
  lvgl_port_unlock();
};

BaseType_t display_submit_cmd(const display_cmd_t *cmd) {
  return xQueueSend(display_queue, cmd, pdMS_TO_TICKS(100));
}

void display_task(void *arg) {
  (void)arg;

  display_cmd_t cmd;

  for (;;) {
    while (xQueueReceive(display_queue, &cmd, portMAX_DELAY)) {

      // Process tasks
      switch (cmd.type) {
        case DISPLAY_CMD_SET_MESSAGE:
        case DISPLAY_CMD_SET_SCREEN:
        default:
      }      
    }
  }
  
}
