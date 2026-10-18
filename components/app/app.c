#include "driver/gpio.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "display.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "app.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "portmacro.h"
#include "queue.h"
#include "soc/gpio_num.h"
#include "rest.h"

#define DIM_DELAY (1000 * 10)
#define SLEEP_DELAY (1000 * 15)
#define AWAKE_MAIL_POLL_INTERVAL (1000 * 60)
#define MAIL_POLL_TIMEOUT (1000 * 10)

#define US_IN_S 1000000
// Poll mail every 10 seconds while sleeping (short interval for development).
#define SLEEP_TIMER_DELAY (US_IN_S * 10)

static const char *TAG = "app_timer";

static QueueHandle_t app_queue;
static app_mail_queue_t mail_queue;

static TimerHandle_t dim_timer_handler;
static TimerHandle_t sleep_timer_handler;
static TimerHandle_t awake_mail_poll_timer_handler;
static TimerHandle_t mail_poll_timer_handler; // Fires if mail poll taking too long
static TickType_t last_interaction;
static bool mail_poll_in_progress;

static void mail_rest_result_received(const mail_rest_result_t *result, void *context) {
  (void)context;
  app_event_t event = {0};

  if (result->operation == MAIL_REST_POLL) {
    if (result->error != ESP_OK) {
      event.type = APP_EVENT_MAIL_POLL_FAILED;
    } else if (result->has_mail) {
      event.type = APP_EVENT_NEW_MAIL;
      event.mail_content = result->mail;
    } else {
      event.type = APP_EVENT_MAIL_POLL_COMPLETE;
    }
    if (app_create_event(&event) != pdTRUE) {
      ESP_LOGE(TAG, "Could not queue mail poll result.");
    }
  } else if (result->error != ESP_OK) {
    ESP_LOGW(TAG, "Mark read request failed: %s", esp_err_to_name(result->error));
  }
}

static void mail_poll_timeout_callback( TimerHandle_t xTimer ) {
  app_event_t event = {
    .type = APP_EVENT_MAIL_POLL_TIMEOUT
  };

  BaseType_t queued = xQueueSend(app_queue, &event, pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "Mail poll timeout fired at tick %lu; event queued=%d",
           (unsigned long)xTaskGetTickCount(), (int)queued);
}

static void awake_mail_poll_callback( TimerHandle_t xTimer ) {
  app_event_t event = {
    .type = APP_EVENT_MAIL_POLL_DUE
  };

  BaseType_t queued = xQueueSend(app_queue, &event, 0);
  ESP_LOGI(TAG, "Awake mail poll timer fired at tick %lu; event queued=%d",
           (unsigned long)xTaskGetTickCount(), (int)queued);
}

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

  mail_poll_timer_handler = xTimerCreate("mail poll timeout", pdMS_TO_TICKS(MAIL_POLL_TIMEOUT), false, NULL, mail_poll_timeout_callback);
  awake_mail_poll_timer_handler = xTimerCreate("awake mail poll", pdMS_TO_TICKS(AWAKE_MAIL_POLL_INTERVAL), pdTRUE, NULL, awake_mail_poll_callback);
  dim_timer_handler = xTimerCreate("dim timer", pdMS_TO_TICKS(DIM_DELAY), false, NULL, dim_timer_callback);
  sleep_timer_handler = xTimerCreate("sleep timer", pdMS_TO_TICKS(SLEEP_DELAY), false, NULL, sleep_timer_callback);

  amqueue_init(&mail_queue);
  ESP_ERROR_CHECK(mail_rest_init(mail_rest_result_received, NULL));

  ESP_LOGI(TAG, "Timers created: awake-poll=%p dim=%p sleep=%p; intervals=%dms/%dms/%dms",
           (void *)awake_mail_poll_timer_handler,
           (void *)dim_timer_handler, (void *)sleep_timer_handler,
           AWAKE_MAIL_POLL_INTERVAL, DIM_DELAY, SLEEP_DELAY);
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
  BaseType_t awake_poll_started = xTimerStart(awake_mail_poll_timer_handler, 0);
  ESP_LOGI(TAG, "Starting timers at tick %lu: poll=%d dim=%d sleep=%d",
           (unsigned long)last_interaction, (int)awake_poll_started,
           (int)dim_started, (int)sleep_started);

  for (;;) {
    while(xQueueReceive(app_queue, &event, portMAX_DELAY)) {
      switch (event.type) {
      case APP_EVENT_TOUCH_TAP:
        ESP_LOGI(TAG, "Touch event at tick %lu", (unsigned long)xTaskGetTickCount());
        reset_inactivity_timers();

        // If currently on empty screen, then do nothing


        // If on reading screen, go ahead and mark this as read, send to mail queue to mark read on API
        // and then switch to either empty or mail screen (depending on queue)

        // If no more mail, empty screen

        break;
      case APP_EVENT_IDLE_DIM:
        ESP_LOGI(TAG, "Dim event at tick %lu; elapsed=%lu; display_state=%d",
                 (unsigned long)xTaskGetTickCount(),
                 (unsigned long)(xTaskGetTickCount() - last_interaction),
                 (int)display_state());
        if ((xTaskGetTickCount() - last_interaction) >= pdMS_TO_TICKS(DIM_DELAY) &&
            display_state() == DISPLAY_NORMAL) {
          display_dim();
        }
        break;
      case APP_EVENT_IDLE_SLEEP:
        ESP_LOGI(TAG, "Sleep event at tick %lu; elapsed=%lu; display_state=%d",
                 (unsigned long)xTaskGetTickCount(),
                 (unsigned long)(xTaskGetTickCount() - last_interaction),
                 (int)display_state());
        if ((xTaskGetTickCount() - last_interaction) < pdMS_TO_TICKS(SLEEP_DELAY)) {
          ESP_LOGI(TAG, "Ignoring stale sleep event after recent interaction.");
          break;
        }

        // No periodic or in-flight poll timeout should remain active during light sleep.
        xTimerStop(awake_mail_poll_timer_handler, pdMS_TO_TICKS(100));
        xTimerStop(mail_poll_timer_handler, pdMS_TO_TICKS(100));
        mail_poll_in_progress = false;

        if (display_state() != DISPLAY_SLEEP) {
          // Turn off display, begin light sleep
          display_sleep();
        }

        gpio_wakeup_enable(GPIO_NUM_33, GPIO_INTR_LOW_LEVEL);
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(SLEEP_TIMER_DELAY));
        esp_light_sleep_start();

        uint32_t causes = esp_sleep_get_wakeup_causes();
        ESP_LOGI(TAG, "Woke from light sleep; wake causes=0x%08lx",
                 (unsigned long)causes);

        if (causes & (1U << ESP_SLEEP_WAKEUP_GPIO)) {
          reset_inactivity_timers();
          xTimerStart(awake_mail_poll_timer_handler, 0);
          break;
        } else if (causes & (1U << ESP_SLEEP_WAKEUP_TIMER)) {
          ESP_LOGI(TAG, "Woke up on mail poll timer.");
          xTimerStart(awake_mail_poll_timer_handler, 0);
          app_event_t event = {
            .type = APP_EVENT_MAIL_POLL_DUE
          };
          xQueueSend(app_queue, &event, 0);
          break;
        }
        break;
      case APP_EVENT_NEW_MAIL:
        // Immediately STOP mail timeout timer
        xTimerStop(mail_poll_timer_handler, 0);
        mail_poll_in_progress = false;
        if (amqueue_enqueue(&mail_queue, event.mail_content) != QUEUE_OK) {
          ESP_LOGE(TAG, "Mail queue is full; could not store new mail.");
        } else {
          ESP_LOGI(TAG, "Received mail id=%s", event.mail_content.id);
        }
        break;
      case APP_EVENT_MAIL_POLL_DUE:
        if (mail_poll_in_progress) {
          ESP_LOGI(TAG, "Skipping mail poll; previous request is still in progress.");
          break;
        }
        mail_poll_in_progress = true;
        if (xTimerStart(mail_poll_timer_handler, 0) != pdPASS) {
          mail_poll_in_progress = false;
          ESP_LOGE(TAG, "Could not start mail poll timeout timer.");
          break;
        }
        if (mail_rest_poll_async() != ESP_OK) {
          xTimerStop(mail_poll_timer_handler, 0);
          mail_poll_in_progress = false;
          ESP_LOGE(TAG, "Could not queue mail poll request.");
          break;
        }
        ESP_LOGI(TAG, "Mail poll request queued.");
        break;
      case APP_EVENT_MAIL_POLL_COMPLETE:
        xTimerStop(mail_poll_timer_handler, 0);
        mail_poll_in_progress = false;
        ESP_LOGI(TAG, "Mail poll completed; no unread mail.");
        break;
      case APP_EVENT_MAIL_POLL_FAILED:
        xTimerStop(mail_poll_timer_handler, 0);
        mail_poll_in_progress = false;
        ESP_LOGW(TAG, "Mail poll failed; next scheduled poll may retry.");
        break;
      case APP_EVENT_MAIL_POLL_TIMEOUT:
        if (mail_poll_in_progress) {
          mail_poll_in_progress = false;
          // Leave the CPU awake so the user can interact or another poll can be started.
          ESP_LOGW(TAG, "Mail poll timed out; leaving device awake.");
        } else {
          ESP_LOGI(TAG, "Ignoring stale mail poll timeout.");
        }
        break;
      default:
        break;
      }      
    }
  };
}
