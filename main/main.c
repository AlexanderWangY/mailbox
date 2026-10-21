#include "freertos/FreeRTOS.h"
#include "app.h"
#include "esp_err.h"
#include "esp_log.h"
#include "board.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "display.h"
#include "touch.h"
#include "ui.h"
static const char *TAG = "main";

void app_main(void)
{
  ESP_LOGI(TAG, "Startup: initializing board");
  init_board();
  esp_err_t wifi_err = board_wifi_init();
  if (wifi_err != ESP_OK) {
    ESP_LOGW(TAG, "Wi-Fi startup failed; will continue without mail access: %s", esp_err_to_name(wifi_err));
  }
  ESP_LOGI(TAG, "Startup: initializing display");
  display_init();
  ESP_LOGI(TAG, "Startup: initializing UI");
  ui_init();
  ESP_LOGI(TAG, "Startup: loading empty mailbox screen");
  ui_draw_empty_screen();
  display_normal();
  ESP_LOGI(TAG, "Startup: initializing app timers");
  app_init();
  ESP_LOGI(TAG, "Startup: initializing touch");
  touch_init();

  BaseType_t app_task_result = xTaskCreate(app_task, "app task", 4096, NULL, 3, NULL);
  ESP_ERROR_CHECK(app_task_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
