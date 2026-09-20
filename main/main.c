#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "freertos/task.h"
#include "hal/i2c_types.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include <stdint.h>
#include <stdlib.h>

#define LED_GPIO GPIO_NUM_13

#define STEMMA_POWER GPIO_NUM_2

#define I2C_SDA_GPIO GPIO_NUM_22
#define I2C_SCL_GPIO GPIO_NUM_20

#define DRV2605L_ADDR 0x5A
#define DRV2605L_MODE 0x01
#define DRV2605L_GO 0x0C

#define LIS3DH_ADDR 0x18

static const char* TAG = "mailbox";

static void stemma_power_init(void) {
  gpio_reset_pin(STEMMA_POWER);

  gpio_set_direction(STEMMA_POWER, GPIO_MODE_OUTPUT);

  gpio_set_level(STEMMA_POWER, 1);

  vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
  uint8_t data[2] = {
    reg,
    val
  };
  
  return i2c_master_transmit(dev, data, sizeof(data), 100);
}

uint8_t i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg) {
  uint8_t out = 0;

  ESP_ERROR_CHECK(i2c_master_transmit_receive(
    dev,
    &reg, 1,
    &out, 1,
    100
  ));

  return out;
}

void app_main(void)
{
  stemma_power_init();
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

  // I2C setup
  i2c_master_bus_config_t i2c_mst_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = I2C_SCL_GPIO,
    .sda_io_num = I2C_SDA_GPIO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };

  i2c_master_bus_handle_t bus_handle;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = DRV2605L_ADDR,
    .scl_speed_hz = 100000,
  };

  i2c_master_dev_handle_t dev_handle;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

  esp_rom_delay_us(1000);

  // remove from standby mode
  i2c_write_reg(dev_handle, DRV2605L_MODE, 0x00);

  // Calibration
  i2c_write_reg(dev_handle, DRV2605L_MODE, 0x07);
  //TODO: Actually add calibration values
  // LIKE ACTUALLY WHAT THE FUCK AM I READING IN THE DATASHEET BRO

  // Set feedback control (0x1A) to ERM mode
  uint8_t fb_ctrl = i2c_read_reg(dev_handle, 0x1A);
  
  // Clear ERM_LRA, FB_BRAKE_FACTOR and LOOP_GAIN
  fb_ctrl &= ~0xFC;
  
  // ERM_LRA = 0
  // FB_BRAKE_FACTOR = 2
  // LOOP_GAIN = 2
  fb_ctrl |= (2 << 4) | (2 << 2);
  
  ESP_ERROR_CHECK(
      i2c_write_reg(dev_handle, 0x1A, fb_ctrl)
  );

  // FB brake factor default should be fine so dont do anything

  // Set rated voltage to 3V
  i2c_write_reg(dev_handle, 0x16, 142);

  uint8_t ctrl3 = i2c_read_reg(dev_handle, 0x1D);
  
  // Clear ERM_OPEN_LOOP
  ctrl3 &= ~(1 << 5);
  
  ESP_ERROR_CHECK(
      i2c_write_reg(dev_handle, 0x1D, ctrl3)
  );

  uint8_t ctrl4 = i2c_read_reg(dev_handle, 0x1E);
  
  // AUTO_CAL_TIME = 3
  ctrl4 = (ctrl4 & ~0x30) | (3 << 4);
  
  ESP_ERROR_CHECK(
      i2c_write_reg(dev_handle, 0x1E, ctrl4)
  );

  // Default loop gain OK
  
  // Start auto-calibration process
  ESP_LOGI(TAG, "MODE:        0x%02X",
           i2c_read_reg(dev_handle, 0x01));
  
  ESP_LOGI(TAG, "RATED:       0x%02X",
           i2c_read_reg(dev_handle, 0x16));
  
  ESP_LOGI(TAG, "OD_CLAMP:    0x%02X",
           i2c_read_reg(dev_handle, 0x17));
  
  ESP_LOGI(TAG, "FEEDBACK:    0x%02X",
           i2c_read_reg(dev_handle, 0x1A));
  
  ESP_LOGI(TAG, "CONTROL1:    0x%02X",
           i2c_read_reg(dev_handle, 0x1B));
  
  ESP_LOGI(TAG, "CONTROL2:    0x%02X",
           i2c_read_reg(dev_handle, 0x1C));
  
  ESP_LOGI(TAG, "CONTROL3:    0x%02X",
           i2c_read_reg(dev_handle, 0x1D));
  
  ESP_LOGI(TAG, "CONTROL4:    0x%02X",
           i2c_read_reg(dev_handle, 0x1E));
  ESP_LOGI(TAG, "Starting calibration");

  
  ESP_ERROR_CHECK(
      i2c_write_reg(dev_handle, DRV2605L_GO, 0x01)
  );
  
  // Wait for calibration
  uint8_t go;
  
  do {
      vTaskDelay(pdMS_TO_TICKS(10));
      go = i2c_read_reg(dev_handle, DRV2605L_GO);
  } while (go & 0x01);
  
  // Read the results
  uint8_t status = i2c_read_reg(dev_handle, 0x00);
  
  uint8_t cal_comp = i2c_read_reg(dev_handle, 0x18);
  uint8_t cal_bemf = i2c_read_reg(dev_handle, 0x19);
  uint8_t feedback = i2c_read_reg(dev_handle, 0x1A);
  
  ESP_LOGI(TAG, "STATUS:     0x%02X", status);
  ESP_LOGI(TAG, "A_CAL_COMP: 0x%02X", cal_comp);
  ESP_LOGI(TAG, "A_CAL_BEMF: 0x%02X", cal_bemf);
  ESP_LOGI(TAG, "FEEDBACK:   0x%02X", feedback);
  
  if (status & 0x08) {
      ESP_LOGE(TAG, "Calibration failed!");
      return;
  }
  
  ESP_LOGI(TAG, "Calibration successful!");
  for (;;);
}
