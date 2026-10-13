#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "freertos/projdefs.h"
#include "display.h"
#include <math.h>
#include <stdint.h>


void app_main(void)
{
  init_board();
  display_init();
  display_text("Hello there Alex! Three t four pages what is commoduty fetishizsm in relation to marx. its not really asking a analyzing question. why");
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
