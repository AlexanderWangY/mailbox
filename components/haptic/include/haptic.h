#pragma once

#include "esp_err.h"
#include <stdint.h>
typedef struct {
  uint8_t comp;
  uint8_t bemf;
  uint8_t feedback;
} HapticCalibration;

esp_err_t save_calib(const HapticCalibration *calib);
esp_err_t load_calib(HapticCalibration *calib);
// Saves result into calib
esp_err_t run_calib(HapticCalibration *calib);
void haptic_init(void);

esp_err_t haptic_play(uint8_t effect);
