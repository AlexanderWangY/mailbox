#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include <stdint.h>
#include <stdio.h>
#include "board.h"

#define LED_GPIO GPIO_NUM_13
#define STEMMA_POWER GPIO_NUM_2

#define I2C_SDA_GPIO GPIO_NUM_22
#define I2C_SCL_GPIO GPIO_NUM_20

#define DRV2605L_ADDR 0x5A
#define DRV2605L_MODE 0x01
#define DRV2605L_GO 0x0C

#define LIS3DH_ADDR 0x18

static i2c_master_bus_handle_t i2c_bus;
static const char *TAG = "mailbox";

static void stemma_power_init(void) {
  gpio_reset_pin(STEMMA_POWER);
  gpio_set_direction(STEMMA_POWER, GPIO_MODE_OUTPUT);
  gpio_set_level(STEMMA_POWER, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
}

static void led_init(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

void init_board(void) {
    ESP_LOGI(TAG, "Initializing LED.");
    led_init();
    ESP_LOGI(TAG, "Initializing STEMMA QT power.");
    stemma_power_init();

  i2c_master_bus_config_t i2c_mst_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = I2C_SCL_GPIO,
    .sda_io_num = I2C_SDA_GPIO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };

  ESP_ERROR_CHECK(
    i2c_new_master_bus(&i2c_mst_config, &i2c_bus)
  );
}

void i2c_attach_device(i2c_device_config_t cfg, i2c_master_dev_handle_t *handle) {
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &cfg, handle));
}

esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
  uint8_t data[2] = {
    reg,
    val,
  };

  return i2c_master_transmit(dev, data, sizeof(data), 100);
}

I2cReadResult i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg) {
  uint8_t out = 0;

  esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, &out, 1, 100);

  if (err != ESP_OK) {
    return (I2cReadResult){ .success = false, .err = err };
  }

  return (I2cReadResult) { .success = true, .ok = out};
}

esp_err_t i2c_set_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask) {
  I2cReadResult res = i2c_read_reg(dev, reg);
  if (!res.success) {
    return res.err;
  }

  return i2c_write_reg(dev, reg, res.ok | mask);
}

esp_err_t i2c_clear_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask) {
  I2cReadResult res = i2c_read_reg(dev, reg);
  if (!res.success) {
    return res.err;
  }

  return i2c_write_reg(dev, reg, res.ok & ~mask);
}

esp_err_t i2c_update_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask, uint8_t val) {
  I2cReadResult res = i2c_read_reg(dev, reg);
  if (!res.success) {
    return res.err;
  }

  uint8_t updated = (res.ok & ~mask) | (val & mask);
  return i2c_write_reg(dev, reg, updated);
}
