#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "freertos/projdefs.h"
#include "display.h"
#include "ui.h"
#include <math.h>
#include <stdint.h>

#if LV_FONT_MONTSERRAT_40
#error "ENABLED!"
#endif

#if LV_FONT_MONTSERRAT_14
#error "ENABLED MONT 14!"
#endif

void app_main(void)
{
  init_board();
  display_init();
  ui_init();
  ui_draw_reading_screen();
  ui_set_reading_message("hope you have an amazing day!");
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
