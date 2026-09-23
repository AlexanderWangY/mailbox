#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "freertos/projdefs.h"
#include "haptic.h"
#include <stdint.h>

void app_main(void)
{
  init_board();
  ESP_ERROR_CHECK(haptic_play(16));
  ESP_ERROR_CHECK(haptic_play(16));
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
