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
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "include/lvgl/core/lv_area.h"
#include "include/lvgl/core/lv_obj_pos.h"
#include "include/lvgl/display/lv_display.h"
#include "include/lvgl/draw/lv_color.h"
#include "include/lvgl/draw/lv_image_dsc.h"
#include "include/lvgl/font/lv_font.h"
#include "include/lvgl/widgets/lv_image.h"
#include "include/lvgl/widgets/lv_label.h"

#define LCD_BACKLIGHT_GPIO GPIO_NUM_26
#define BUFFER_SIZE (LCD_WIDTH * LCD_TRANSFER_LINES)

static lv_display_t *lv_disp;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static const char *TAG = "display";

extern const uint8_t mail_png_start[] asm("_binary_Mail_png_start");
extern const uint8_t mail_png_end[] asm("_binary_Mail_png_end");
extern const uint8_t mail_rgb565_start[] asm("_binary_Mail_rgb565_start");
extern const uint8_t mail_rgb565_end[] asm("_binary_Mail_rgb565_end");

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
  lv_disp = lvgl_port_add_disp(&disp_cfg);

  if (lvgl_port_lock(0)) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(lv_disp);
    lvgl_port_unlock();
  }
  ESP_LOGI(TAG, "LVGL initialized.");
  size_t mail_png_size;
  (void)display_mail_png(&mail_png_size);
  ESP_LOGI(TAG, "Embedded Mail.png (%u bytes)", (unsigned)mail_png_size);
  ESP_LOGI(TAG, "Display initialized.");
}

void display_sleep(void) {
  ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 0));
  lvgl_port_stop();
  ESP_ERROR_CHECK(esp_lcd_panel_disp_sleep(panel_handle, true));
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

void display_clear() {
  lvgl_port_lock(0);
  lv_obj_clean(lv_screen_active());
  lvgl_port_unlock();
};

void display_text(const char *text) {
  (void)text;
  if (!lvgl_port_lock(0)) {
    return;
  }

  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);

  static lv_image_dsc_t mail_image;
  mail_image.header.magic = LV_IMAGE_HEADER_MAGIC;
  mail_image.header.cf = LV_COLOR_FORMAT_RGB565;
  mail_image.header.w = 272;
  mail_image.header.h = 168;
  mail_image.header.stride = 272 * sizeof(uint16_t);
  mail_image.data_size = (uint32_t)(mail_rgb565_end - mail_rgb565_start);
  mail_image.data = mail_rgb565_start;

  lv_obj_t *image = lv_image_create(screen);
  lv_image_set_src(image, &mail_image);
  lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
  lvgl_port_unlock();
};

const uint8_t *display_mail_png(size_t *size) {
  if (size != NULL) {
    *size = (size_t)(mail_png_end - mail_png_start);
  }
  return mail_png_start;
}

