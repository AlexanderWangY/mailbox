#include "freertos/FreeRTOS.h"
#include "app.h"
#include "esp_err.h"
#include "esp_log.h"
#include "board.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "display.h"
#include "touch.h"
#include "ui.h"
#include <math.h>
#include <stdint.h>

#if LV_FONT_MONTSERRAT_40
#error "ENABLED!"
#endif

#if LV_FONT_MONTSERRAT_14
#error "ENABLED MONT 14!"
#endif

static TaskHandle_t app_task_handle;
static const char *TAG = "main";

void app_main(void)
{
  ESP_LOGI(TAG, "Startup: initializing board");
  init_board();
  ESP_LOGI(TAG, "Startup: initializing display");
  display_init();
  ESP_LOGI(TAG, "Startup: initializing UI");
  ui_init();
  ESP_LOGI(TAG, "Startup: loading reading screen");
  ui_draw_reading_screen();
  ui_set_reading_message("hope you have an amazing day!");
  ESP_LOGI(TAG, "Startup: initializing app timers");
  app_init();
  ESP_LOGI(TAG, "Startup: initializing touch");
  touch_init();

  BaseType_t app_task_result = xTaskCreate(app_task, "app task", 4096, NULL, 3, &app_task_handle);
  ESP_LOGI(TAG, "App task creation result: %d", (int)app_task_result);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
