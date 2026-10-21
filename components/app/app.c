#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "display.h"
#include "ui.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "haptic.h"
#include "portmacro.h"
#include "queue.h"
#include "soc/gpio_num.h"
#include "rest.h"

#define DIM_DELAY (1000 * 10)
#define SLEEP_DELAY (1000 * 15)
#define AWAKE_MAIL_POLL_INTERVAL (1000 * 60)
#define MAIL_POLL_TIMEOUT (1000 * 30)
#define NEW_MAIL_HAPTIC_EFFECT 47
#define WAKE_TOUCH_WINDOW_MS 500

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
static size_t pending_mail_reads;
static bool has_current_mail;
static mail_t current_mail;
static ui_screen_t current_screen = UI_SCREEN_NO_MAIL;
static bool suppress_wake_touch;
static uint32_t touch_wake_tick;

static bool mail_id_is_queued(const char *mail_id) {
  for (size_t index = mail_queue.head; index != mail_queue.tail;
       index = (index + 1) % BUFFER_SIZE) {
    if (strcmp(mail_queue.data[index].id, mail_id) == 0) return true;
  }
  return false;
}

static void show_next_mail(void) {
  if (amqueue_dequeue(&mail_queue, &current_mail) == QUEUE_OK) {
    has_current_mail = true;
    current_screen = UI_SCREEN_HAS_MAIL;
    ui_draw_mail_screen();
    return;
  }

  has_current_mail = false;
  current_screen = UI_SCREEN_NO_MAIL;
  ui_set_reading_message("");
  ui_draw_empty_screen();
}

static bool finish_reading_current_mail(void) {
  esp_err_t err = mail_rest_mark_read_async(current_mail.id);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not queue mark-read request: %s", esp_err_to_name(err));
    return false;
  }

  pending_mail_reads++;
  has_current_mail = false;
  show_next_mail();
  return true;
}

static void resume_sleep_if_idle(void) {
  if (pending_mail_reads == 0 &&
      (xTaskGetTickCount() - last_interaction) >= pdMS_TO_TICKS(SLEEP_DELAY)) {
    app_event_t sleep_event = {.type = APP_EVENT_IDLE_SLEEP};
    xQueueSend(app_queue, &sleep_event, 0);
  }
}

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
    xQueueSend(app_queue, &event, portMAX_DELAY);
  } else {
    event.type = result->error == ESP_OK ? APP_EVENT_MAIL_READ_COMPLETE : APP_EVENT_MAIL_READ_FAILED;
    xQueueSend(app_queue, &event, portMAX_DELAY);
  }
}

static void wifi_ready_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_base;
  (void)event_id;
  (void)event_data;
  app_event_t event = {.type = APP_EVENT_MAIL_POLL_DUE};
  if (xQueueSend(app_queue, &event, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Could not queue poll after Wi-Fi connected.");
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
  ESP_ERROR_CHECK(app_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
  if (board_wifi_started()) {
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_ready_event_handler, NULL));
  }

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
  app_event_t initial_poll = {.type = APP_EVENT_MAIL_POLL_DUE};
  app_create_event(&initial_poll);

  for (;;) {
    while(xQueueReceive(app_queue, &event, portMAX_DELAY)) {
      switch (event.type) {
      case APP_EVENT_TOUCH_TAP: {
        ESP_LOGI(TAG, "Touch event at tick %lu", (unsigned long)xTaskGetTickCount());
        if (suppress_wake_touch) {
          if ((int32_t)(event.touch_tick - touch_wake_tick) <=
              (int32_t)pdMS_TO_TICKS(WAKE_TOUCH_WINDOW_MS)) {
            ESP_LOGI(TAG, "Ignoring the touch that woke the display.");
            break;
          }
          suppress_wake_touch = false;
        }
        display_state_t state_before_tap = display_state();
        reset_inactivity_timers();
        if (state_before_tap != DISPLAY_NORMAL) break;

        if (current_screen == UI_SCREEN_HAS_MAIL && has_current_mail) {
          current_screen = UI_SCREEN_READING;
          ui_set_reading_message(current_mail.content);
          ui_draw_reading_screen();
        } else if (current_screen == UI_SCREEN_READING && has_current_mail) {
          finish_reading_current_mail();
        } else if (!has_current_mail && !mail_poll_in_progress) {
          app_event_t poll_event = {.type = APP_EVENT_MAIL_POLL_DUE};
          app_create_event(&poll_event);
        }

        break;
      }
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

        if (current_screen == UI_SCREEN_READING && has_current_mail &&
            !finish_reading_current_mail()) {
          xTimerReset(sleep_timer_handler, 0);
          break;
        }

        if (mail_poll_in_progress || pending_mail_reads != 0) {
          ESP_LOGI(TAG, "Deferring sleep until mail request completes.");
          xTimerReset(sleep_timer_handler, 0);
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
        ESP_ERROR_CHECK(board_wifi_stop_for_sleep());
        esp_err_t sleep_err = esp_light_sleep_start();
        ESP_ERROR_CHECK(board_wifi_resume_after_sleep());
        ESP_ERROR_CHECK(sleep_err);

        uint32_t causes = esp_sleep_get_wakeup_causes();
        ESP_LOGI(TAG, "Woke from light sleep; wake causes=0x%08lx",
                 (unsigned long)causes);

        if (causes & (1U << ESP_SLEEP_WAKEUP_GPIO)) {
          touch_wake_tick = (uint32_t)xTaskGetTickCount();
          suppress_wake_touch = true;
          reset_inactivity_timers();
          xTimerStart(awake_mail_poll_timer_handler, 0);
          break;
        } else if (causes & (1U << ESP_SLEEP_WAKEUP_TIMER)) {
          ESP_LOGI(TAG, "Woke up on mail poll timer.");
          xTimerReset(sleep_timer_handler, 0);
          xTimerStart(awake_mail_poll_timer_handler, 0);
          app_event_t event = {
            .type = APP_EVENT_MAIL_POLL_DUE
          };
          xQueueSend(app_queue, &event, 0);
          break;
        }
        break;
      case APP_EVENT_NEW_MAIL: {
        // Immediately STOP mail timeout timer
        xTimerStop(mail_poll_timer_handler, 0);
        mail_poll_in_progress = false;
        if ((has_current_mail && strcmp(current_mail.id, event.mail_content.id) == 0) ||
            mail_id_is_queued(event.mail_content.id)) {
          ESP_LOGI(TAG, "Ignoring duplicate mail id=%s", event.mail_content.id);
        } else if (amqueue_enqueue(&mail_queue, event.mail_content) != QUEUE_OK) {
          ESP_LOGE(TAG, "Mail queue is full; could not store new mail.");
        } else {
          ESP_LOGI(TAG, "Received mail id=%s", event.mail_content.id);
          esp_err_t haptic_err = haptic_play(NEW_MAIL_HAPTIC_EFFECT);
          if (haptic_err != ESP_OK) {
            ESP_LOGW(TAG, "Mail haptic failed: %s", esp_err_to_name(haptic_err));
          }
          if (current_screen != UI_SCREEN_READING) {
            reset_inactivity_timers();
          }
          if (!has_current_mail) show_next_mail();
        }
        break;
      }
      case APP_EVENT_MAIL_POLL_DUE:
        if (amqueue_full(&mail_queue)) {
          ESP_LOGI(TAG, "Skipping mail poll; app mail queue is full.");
          break;
        }
        if (!board_wifi_connected()) {
          ESP_LOGI(TAG, "Skipping mail poll until Wi-Fi has an IP address.");
          break;
        }
        if (has_current_mail || pending_mail_reads != 0) {
          ESP_LOGI(TAG, "Skipping mail poll while a message is open or a read request is pending.");
          break;
        }
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
      case APP_EVENT_MAIL_READ_COMPLETE: {
        if (pending_mail_reads > 0) pending_mail_reads--;
        if (pending_mail_reads == 0 && !has_current_mail) {
          if ((xTaskGetTickCount() - last_interaction) < pdMS_TO_TICKS(SLEEP_DELAY)) {
            app_event_t poll_event = {.type = APP_EVENT_MAIL_POLL_DUE};
            app_create_event(&poll_event);
          }
        }
        resume_sleep_if_idle();
        break;
      }
      case APP_EVENT_MAIL_READ_FAILED: {
        if (pending_mail_reads > 0) pending_mail_reads--;
        ESP_LOGW(TAG, "Mark-read request failed; the unread message will be fetched again.");
        if (pending_mail_reads == 0 && !has_current_mail) {
          if ((xTaskGetTickCount() - last_interaction) < pdMS_TO_TICKS(SLEEP_DELAY)) {
            app_event_t poll_event = {.type = APP_EVENT_MAIL_POLL_DUE};
            app_create_event(&poll_event);
          }
        }
        resume_sleep_if_idle();
        break;
      }
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
          // The worker's network request has its own finite timeout. Keep the
          // in-flight flag until its result arrives so retries cannot duplicate it.
          ESP_LOGW(TAG, "Mail poll is taking longer than expected; awaiting worker result.");
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
