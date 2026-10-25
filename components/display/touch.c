#include "touch.h"

#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft6x36.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TOUCH_IRQ GPIO_NUM_33
#define TOUCH_RESET GPIO_NUM_13

typedef enum {
  TOUCH_ACTIVE,
  TOUCH_SLEEPING,
} touch_mode_t;

static const char *TAG = "touch";
static esp_lcd_touch_handle_t controller;
static TaskHandle_t task;
static SemaphoreHandle_t lock;
static touch_mode_t mode;
static bool consume_until_release;
static uint32_t sequence; // Protected by lock, like mode and consume_until_release.
static touch_callback_t on_tap;
static void *callback_context;

static void touch_isr(void *arg) {
  (void)arg;
  // Mask the pin until the task services it, so a noisy IRQ cannot monopolize
  // the CPU. GPIO wake detection remains independent of the task notification.
  gpio_intr_disable(TOUCH_IRQ);
  BaseType_t yield = pdFALSE;
  vTaskNotifyGiveFromISR(task, &yield);
  if (yield) portYIELD_FROM_ISR();
}

static void touch_task(void *arg) {
  (void)arg;
  bool pressed = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pressed ? pdMS_TO_TICKS(20) : portMAX_DELAY);
    xSemaphoreTake(lock, portMAX_DELAY);
    if (mode == TOUCH_SLEEPING) {
      pressed = false;
      xSemaphoreGive(lock);
      continue;
    }

    esp_lcd_touch_point_data_t point_data[2];
    uint8_t points = 0;
    esp_err_t err = esp_lcd_touch_read_data(controller);
    if (err == ESP_OK) err = esp_lcd_touch_get_data(controller, point_data, &points, 2);
    bool down = err == ESP_OK && points != 0;
    if (err != ESP_OK) ESP_LOGW(TAG, "Touch read failed: %s", esp_err_to_name(err));
    if (down && !pressed && !consume_until_release) {
      on_tap(++sequence, callback_context);
    }
    if (!down && err == ESP_OK) consume_until_release = false;
    pressed = down;
    xSemaphoreGive(lock);

    if (!pressed) {
      // Bound spurious notifications; there is no periodic read while idle.
      vTaskDelay(pdMS_TO_TICKS(20));
      xSemaphoreTake(lock, portMAX_DELAY);
      if (mode == TOUCH_ACTIVE) ESP_ERROR_CHECK(gpio_intr_enable(TOUCH_IRQ));
      xSemaphoreGive(lock);
    }
  }
}

void touch_init(touch_callback_t callback, void *context) {
  on_tap = callback;
  callback_context = context;
  lock = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(lock != NULL ? ESP_OK : ESP_ERR_NO_MEM);

  const gpio_config_t reset = {
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = 1ULL << TOUCH_RESET,
  };
  ESP_ERROR_CHECK(gpio_config(&reset));
  ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET, 0));
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET, 1));
  vTaskDelay(pdMS_TO_TICKS(300));

  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();
  io_config.dev_addr = 0x38;
  io_config.scl_speed_hz = 100000;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(board_i2c_bus_handle(), &io_config, &io));
  esp_lcd_touch_config_t config = {
    .x_max = LCD_WIDTH,
    .y_max = LCD_HEIGHT,
    .rst_gpio_num = GPIO_NUM_NC,
    .int_gpio_num = TOUCH_IRQ,
    .levels.interrupt = 0,
    .flags.swap_xy = true,
  };
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft6x36(io, &config, &controller));
  ESP_ERROR_CHECK(gpio_intr_disable(TOUCH_IRQ));
  ESP_ERROR_CHECK(gpio_set_pull_mode(TOUCH_IRQ, GPIO_PULLUP_ONLY));
  ESP_ERROR_CHECK(gpio_set_intr_type(TOUCH_IRQ, GPIO_INTR_NEGEDGE));
  ESP_ERROR_CHECK(xTaskCreate(touch_task, "touch", 4096, NULL, 3, &task) == pdPASS
                  ? ESP_OK : ESP_ERR_NO_MEM);
  esp_err_t err = gpio_install_isr_service(0);
  ESP_ERROR_CHECK(err == ESP_ERR_INVALID_STATE ? ESP_OK : err);
  ESP_ERROR_CHECK(gpio_isr_handler_add(TOUCH_IRQ, touch_isr, NULL));
  ESP_ERROR_CHECK(gpio_intr_enable(TOUCH_IRQ));
  xTaskNotifyGive(task);
}

bool touch_prepare_sleep(uint32_t consumed_sequence) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ready = sequence == consumed_sequence;
  if (ready) {
    mode = TOUCH_SLEEPING;
    ESP_ERROR_CHECK(gpio_intr_disable(TOUCH_IRQ));
    ESP_ERROR_CHECK(gpio_wakeup_enable(TOUCH_IRQ, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
  }
  xSemaphoreGive(lock);
  return ready;
}

uint32_t touch_finish_sleep(bool consume_wake_press) {
  xSemaphoreTake(lock, portMAX_DELAY);
  ESP_ERROR_CHECK(gpio_wakeup_disable(TOUCH_IRQ));
  ESP_ERROR_CHECK(gpio_set_intr_type(TOUCH_IRQ, GPIO_INTR_NEGEDGE));
  consume_until_release = consume_wake_press;
  mode = TOUCH_ACTIVE;
  ESP_ERROR_CHECK(gpio_intr_enable(TOUCH_IRQ));
  xTaskNotifyGive(task);
  uint32_t consumed = sequence;
  xSemaphoreGive(lock);
  return consumed;
}
