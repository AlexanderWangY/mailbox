#include "driver/gpio.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "display.h"
#include <stdint.h>
#include <stdio.h>
#include "app.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "portmacro.h"
#include "soc/gpio_num.h"
#include "ui.h"

#define DIM_DELAY (1000 * 10)
#define SLEEP_DELAY (1000 * 15)

static const char *TAG = "app_timer";

static QueueHandle_t app_queue;

static TimerHandle_t dim_timer_handler;
static TimerHandle_t sleep_timer_handler;
static TickType_t last_interaction;

static void dim_timer_callback( TimerHandle_t xTimer )
{
  app_event_t event = {
    .type = APP_EVENT_IDLE_DIM
  };

  BaseType_t queued = xQueueSend(app_queue, &event, pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "Dim timer fired at tick %lu; event queued=%d",
           (unsigned long)xTaskGetTickCount(), (int)queued);
}

static void sleep_timer_callback( TimerHandle_t xTimer )
{
  app_event_t event = {
    .type = APP_EVENT_IDLE_SLEEP
  };

  BaseType_t queued = xQueueSend(app_queue, &event, pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "Sleep timer fired at tick %lu; event queued=%d",
           (unsigned long)xTaskGetTickCount(), (int)queued);
  
}

static void reset_inactivity_timers(void) {
  display_state_t state = display_state();

  last_interaction = xTaskGetTickCount();

  // A tap wakes a sleeping display or restores normal brightness after dimming.
  if (state == DISPLAY_SLEEP) {
    display_wake();
  } else if (state == DISPLAY_DIM) {
    display_normal();
  }

  xTimerReset(dim_timer_handler, 0);
  xTimerReset(sleep_timer_handler, 0);
}


void app_init() {
  app_queue = xQueueCreate(16, sizeof(app_event_t));

  dim_timer_handler = xTimerCreate("dim timer", pdMS_TO_TICKS(DIM_DELAY), false, NULL, dim_timer_callback);
  sleep_timer_handler = xTimerCreate("sleep timer", pdMS_TO_TICKS(SLEEP_DELAY), false, NULL, sleep_timer_callback);

  ESP_LOGI(TAG, "Timers created: dim=%p sleep=%p; delays=%dms/%dms",
           (void *)dim_timer_handler, (void *)sleep_timer_handler,
           DIM_DELAY, SLEEP_DELAY);
};

BaseType_t app_create_event(const app_event_t *event) {
  return xQueueSend(app_queue, event, pdMS_TO_TICKS(100));
}

void app_task(void *arg) {
  (void)arg;

  app_event_t event;

  // Start timers
  last_interaction = xTaskGetTickCount();
  BaseType_t dim_started = xTimerStart(dim_timer_handler, 0);
  BaseType_t sleep_started = xTimerStart(sleep_timer_handler, 0);
  ESP_LOGI(TAG, "Starting timers at tick %lu: dim=%d sleep=%d",
           (unsigned long)last_interaction, (int)dim_started, (int)sleep_started);

  for (;;) {
    while(xQueueReceive(app_queue, &event, portMAX_DELAY)) {
      switch (event.type) {
      case APP_EVENT_TOUCH_TAP:
        ESP_LOGI(TAG, "Touch event at tick %lu", (unsigned long)xTaskGetTickCount());
        reset_inactivity_timers();

        //TODO: FROM HERE
        break;
      case APP_EVENT_IDLE_DIM:
        ESP_LOGI(TAG, "Dim event at tick %lu; elapsed=%lu; display_state=%d",
                 (unsigned long)xTaskGetTickCount(),
                 (unsigned long)(xTaskGetTickCount() - last_interaction),
                 (int)display_state());
        if (display_state() == DISPLAY_NORMAL) {
          display_dim();
        }
        break;
      case APP_EVENT_IDLE_SLEEP:
        ESP_LOGI(TAG, "Sleep event at tick %lu; elapsed=%lu; display_state=%d",
                 (unsigned long)xTaskGetTickCount(),
                 (unsigned long)(xTaskGetTickCount() - last_interaction),
                 (int)display_state());
        if (display_state() != DISPLAY_SLEEP) {
          // Turn off display, begin light sleep
          display_sleep();
        }

        ui_draw_empty_screen();

        gpio_wakeup_enable(GPIO_NUM_33, GPIO_INTR_LOW_LEVEL);
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        esp_sleep_enable_timer_wakeup(10ULL * 60 * 1000000); // 10 minutes to poll for mail
        esp_light_sleep_start();

        uint32_t causes = esp_sleep_get_wakeup_causes();
        ESP_LOGI(TAG, "Woke from light sleep; wake causes=0x%08lx",
                 (unsigned long)causes);

        if (causes & (1U << ESP_SLEEP_WAKEUP_GPIO)) {
          reset_inactivity_timers();
          break;
        } else if (causes & (1U << ESP_SLEEP_WAKEUP_TIMER)) {
          // Timer rang to poll for mail
          break;
        }
        break;
      default:
        break;
      }      
    }
  };
}
