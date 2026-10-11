#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "freertos/projdefs.h"
#include "display.h"
#include <stdint.h>

void app_main(void)
{
  init_board();
  display_init();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
