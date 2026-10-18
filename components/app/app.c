#include "display.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include "app.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"

#define DIM_DELAY (1000 * 30)
#define SLEEP_DELAY (1000 * 45)

static QueueHandle_t app_queue;

static TimerHandle_t dim_timer_handler;
static TimerHandle_t sleep_timer_handler;

static void dim_timer_callback( TimerHandle_t xTimer )
{
  app_event_t event = {
    .type = APP_EVENT_IDLE_DIM
  };

  xQueueSend(app_queue, &event, pdMS_TO_TICKS(100));
}

static void sleep_timer_callback( TimerHandle_t xTimer )
{
  app_event_t event = {
    .type = APP_EVENT_IDLE_SLEEP
  };

  xQueueSend(app_queue, &event, pdMS_TO_TICKS(100));
  
}

static void reset_inactivity_timers(void) {
  display_state_t state = display_state();

  // Undim if dim
  if (state == DISPLAY_DIM) {
    display_normal();
  }

  xTimerReset(dim_timer_handler, 0);
  xTimerReset(sleep_timer_handler, 0);
}


void app_init() {
  app_queue = xQueueCreate(16, sizeof(app_event_t));

  dim_timer_handler = xTimerCreate("dim timer", pdMS_TO_TICKS(DIM_DELAY), false, NULL, dim_timer_callback);
  sleep_timer_handler = xTimerCreate("sleep timer", pdMS_TO_TICKS(SLEEP_DELAY), false, NULL, sleep_timer_callback);
};

BaseType_t app_create_event(const app_event_t *event) {
  return xQueueSend(app_queue, event, pdMS_TO_TICKS(100));
}

void app_task(void *arg) {
  (void)arg;

  app_event_t event;

  // Start timers
  xTimerStart(dim_timer_handler, 0);
  xTimerStart(sleep_timer_handler, 0);

  for (;;) {
    while(xQueueReceive(app_queue, &event, portMAX_DELAY)) {
      switch (event.type) {
      case APP_EVENT_TOUCH_TAP:
        reset_inactivity_timers();

        //TODO: FROM HERE
        
      case APP_EVENT_IDLE_DIM:
        display_dim();
        break;
      case APP_EVENT_IDLE_SLEEP:
        // Turn off display, begin light sleep
        display_sleep();
      default:
        break;
      }      
    }
  };
}
