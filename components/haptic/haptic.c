#include <stdint.h>
#include <stdio.h>
#include "haptic.h"
#include "board.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "hal/i2c_types.h"
#include "nvs.h"

#define DRV2605L_ADDR 0x5A
#define DRV2605L_MODE 0x01
#define DRV2605L_GO 0x0C
#define DRV2605L_RATED_VOLTAGE 0x16
#define DRV2605L_FEEDBACK_CONTROL 0x1A

// We use a DRV2605L haptic driver with a coin motor.

static i2c_master_dev_handle_t dev_handle;

esp_err_t save_calib(const HapticCalibration *calib) {
  nvs_handle_t nvs;

  esp_err_t err = nvs_open("nvs", NVS_READWRITE, &nvs);
  if (err != ESP_OK) return err;

  err = nvs_set_blob(nvs, "calibration", calib, sizeof(*calib));

  if (err == ESP_OK) nvs_commit(nvs);
  nvs_close(nvs);

  return err;
}

esp_err_t load_calib(HapticCalibration *calib) {
  nvs_handle_t nvs;

  esp_err_t err = nvs_open("nvs", NVS_READONLY, &nvs);
  if (err != ESP_OK) return err;

  size_t size = sizeof(*calib);
  err = nvs_get_blob(nvs, "calibration", calib, &size);

  nvs_close(nvs);

  
  if (size != sizeof(*calib)) return ESP_ERR_INVALID_SIZE;
  return err;
}

esp_err_t run_calib(HapticCalibration *calib) {
  i2c_write_reg(dev_handle, DRV2605L_MODE, 0x07);

  uint8_t fb_ctrl = (2 << 4) | (2 << 2);

  // Ok so you might be wondering what the hell ((1U << 6) - 1) << 2 is for.
  // Let me explain.
  //
  // The Feedback control register is laid out like this
  // 
  // Bits: 7       6           4         2         0
  //       +-------+-----------+---------+---------+
  //       |ERM_LRA| FB_BRAKE  | LOOP    | BEMF    |
  //       |       | _FACTOR   | _GAIN   | _GAIN   |
  //       +-------+-----------+---------+---------+
  //
  // We want to set ERM_LRA to 0, BRAKE FACTOR to 2, and LOOP_GAIN to 2
  // So we construct a bitmask by shifting 00000001 over 6 so it becomes
  // 0100000, then we subtract 1 so it becomes 00111111, then we shift by 2 so it becomes 11111100
  // Then the update bit function takes our fb_ctrl and applies the mask while also clearing the bits
  // from the original value, then it OR to insert the new values into that mask.
  i2c_update_bits(dev_handle, DRV2605L_FEEDBACK_CONTROL, ((1U << 6) - 1) << 2, fb_ctrl);
  i2c_write_reg(dev_handle, DRV2605L_RATED_VOLTAGE, 0x75);
  
  return ESP_OK;
}

void haptic_init(void) {
  i2c_device_config_t cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = DRV2605L_ADDR,
    .scl_speed_hz = 100'000,
  };

  i2c_attach_device(cfg, &dev_handle);

  esp_rom_delay_us(1000);

  ESP_LOGI("mailbox", "Checking for previously saved calibration values in NVS.");

  // read from nvs flash calibration values
  HapticCalibration calib;
  esp_err_t err = load_calib(&calib);

  // Not calibrated yet
  if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_SIZE) {
        
  }

  // Other errors should just panic cuz idk what the hell is going on atp
  ESP_ERROR_CHECK(err);
}
