#include "app.h"

#include <limits.h>

#include "board.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "haptic.h"
#include "power.h"
#include "rest.h"
#include "state.h"
#include "touch.h"
#include "ui.h"

typedef enum {
  APP_EVENT_TOUCH,
  APP_EVENT_WIFI_READY,
  APP_EVENT_MAIL_RESULT,
} app_event_type_t;

typedef struct {
  app_event_type_t type;
  union {
    uint32_t touch_sequence;
    mail_rest_result_t mail;
  };
} app_event_t;

static const char *TAG = "app";
static QueueHandle_t events;
static app_state_t app;

static void render(void) {
  switch (app.screen) {
  case APP_SCREEN_EMPTY:
    ui_draw_empty_screen();
    break;
  case APP_SCREEN_MAIL:
    ui_draw_mail_screen();
    break;
  case APP_SCREEN_READING:
    ui_set_reading_message(app.inbox[0].content);
    ui_draw_reading_screen();
    break;
  }
}

static void wake_display(int64_t now_us) {
  app_power_t previous = app.power;
  app_state_wake(&app, now_us);
  power_display_transition(previous, app.power);
}

static void handle_touch(uint32_t sequence, int64_t now_us) {
  if ((int32_t)(sequence - app.consumed_touch) <= 0) return;
  app.consumed_touch = sequence;
  app_power_t previous = app.power;
  wake_display(now_us);

  if (previous == APP_POWER_OFF) {
    // A background result, including one already queued, belongs to the old
    // poll. Consume its completion before starting a fresh foreground request.
    if (app.poll == APP_POLL_RUNNING) {
      app.poll = APP_POLL_CANCELING;
      mail_rest_cancel_poll();
      app.next_poll_us = now_us;
    }
    return;
  }
  if (previous == APP_POWER_DIMMED) return;

  switch (app.screen) {
  case APP_SCREEN_EMPTY:
    break;
  case APP_SCREEN_MAIL:
    app_state_open(&app);
    render();
    break;
  case APP_SCREEN_READING:
    if (mail_rest_mark_read_async(&app.inbox[0]) == ESP_OK) {
      app.pending_reads++;
      app_state_dismiss(&app);
      render();
    } else {
      ESP_LOGW(TAG, "Cannot queue mark-read; leaving message open");
    }
    break;
  }
}

static void handle_mail(const mail_rest_result_t *result, int64_t now_us) {
  if (result->operation == MAIL_REST_MARK_READ) {
    app.pending_reads--;
    if (result->status != MAIL_REST_SUCCEEDED) {
      ESP_LOGW(TAG, "Mark-read failed: %s; keeping the message unread",
               esp_err_to_name(result->error));
      app_state_receive(&app, &result->mail);
      render();
    }
    if (app.pending_reads == 0) app.next_poll_us = now_us;
    return;
  }

  app_poll_t previous = app.poll;
  app.poll = APP_POLL_IDLE;
  if (previous == APP_POLL_CANCELING) {
    app.next_poll_us = now_us;
    return;
  }
  if (result->status != MAIL_REST_SUCCEEDED) {
    ESP_LOGW(TAG, "Mail poll failed: %s", esp_err_to_name(result->error));
    return;
  }
  if (result->has_mail && app_state_receive(&app, &result->mail)) {
    wake_display(now_us);
    render();
    esp_err_t err = haptic_play(47);
    if (err != ESP_OK) ESP_LOGW(TAG, "Mail haptic failed: %s", esp_err_to_name(err));
  }
}

static void start_poll(int64_t now_us) {
  app.next_poll_us = now_us + APP_POLL_US;
  esp_err_t err = mail_rest_poll_async();
  app.poll = err == ESP_OK ? APP_POLL_RUNNING : APP_POLL_IDLE;
  if (err != ESP_OK) ESP_LOGW(TAG, "Cannot queue mail poll: %s", esp_err_to_name(err));
  else ESP_LOGI(TAG, "Mail poll started (%s)", app.power == APP_POWER_OFF ? "display off" : "awake");
}

static void service_deadlines(int64_t now_us) {
  app_power_t next = app_state_power_due(&app, now_us);
  power_display_transition(app.power, next);
  app.power = next;

  if (app.power != APP_POWER_OFF && now_us >= app.next_battery_us) {
    int percent;
    esp_err_t err = board_battery_read_percent(&percent);
    if (err == ESP_OK) ui_set_battery_status(percent, false);
    else if (err == ESP_ERR_NOT_FOUND) ui_set_battery_status(-1, false);
    app.next_battery_us = now_us + APP_BATTERY_US;
  }

  if (app.poll == APP_POLL_WAITING_FOR_WIFI) {
    if (board_wifi_connected()) start_poll(now_us);
    else if (now_us >= app.connect_deadline_us) {
      ESP_LOGW(TAG, "No Wi-Fi for mail poll; retrying at the next interval");
      app.poll = APP_POLL_IDLE;
      app.next_poll_us = now_us + APP_POLL_US;
    }
  } else if (app.poll == APP_POLL_IDLE && app.pending_reads == 0 && now_us >= app.next_poll_us) {
    if (app.mail_count == APP_MAIL_CAPACITY || !board_wifi_started()) {
      app.next_poll_us = now_us + APP_POLL_US;
    } else if (board_wifi_connected()) {
      start_poll(now_us);
    } else {
      app.poll = APP_POLL_WAITING_FOR_WIFI;
      app.connect_deadline_us = now_us + APP_CONNECT_US;
    }
  }
}

static TickType_t wait_until(int64_t deadline_us) {
  if (deadline_us == INT64_MAX) return portMAX_DELAY;
  int64_t remaining_us = deadline_us - esp_timer_get_time();
  if (remaining_us <= 0) return 0;
  return (remaining_us + portTICK_PERIOD_MS * 1000LL - 1) /
         (portTICK_PERIOD_MS * 1000LL);
}

static void app_task(void *arg) {
  (void)arg;
  app_state_init(&app, esp_timer_get_time());
  for (;;) {
    service_deadlines(esp_timer_get_time());
    if (app_state_can_sleep(&app)) {
      power_wake_t wake = power_sleep_until(app.next_poll_us, &app.consumed_touch);
      if (wake != POWER_WAKE_TIMER) {
        // The power module lights the display before restarting Wi-Fi.
        app_state_wake(&app, esp_timer_get_time());
      }
      continue;
    }

    app_event_t event;
    if (xQueueReceive(events, &event, wait_until(app_state_deadline(&app))) != pdTRUE) continue;
    int64_t now_us = esp_timer_get_time();
    switch (event.type) {
    case APP_EVENT_TOUCH:
      handle_touch(event.touch_sequence, now_us);
      break;
    case APP_EVENT_MAIL_RESULT:
      handle_mail(&event.mail, now_us);
      break;
    case APP_EVENT_WIFI_READY:
      // Connectivity wakes the loop; the existing poll deadline still applies.
      break;
    }
  }
}

static void touch_received(uint32_t sequence, void *context) {
  (void)context;
  app_event_t event = {.type = APP_EVENT_TOUCH, .touch_sequence = sequence};
  if (xQueueSend(events, &event, 0) != pdTRUE) ESP_LOGW(TAG, "Touch event queue full");
}

static void mail_received(const mail_rest_result_t *result, void *context) {
  (void)context;
  app_event_t event = {.type = APP_EVENT_MAIL_RESULT, .mail = *result};
  xQueueSend(events, &event, portMAX_DELAY);
}

static void wifi_ready(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  (void)id;
  (void)data;
  app_event_t event = {.type = APP_EVENT_WIFI_READY};
  xQueueSend(events, &event, 0);
}

void app_start(void) {
  events = xQueueCreate(16, sizeof(app_event_t));
  ESP_ERROR_CHECK(events != NULL ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(mail_rest_init(mail_received, NULL));
  if (board_wifi_started()) {
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_ready, NULL));
  }
  touch_init(touch_received, NULL);
  ESP_ERROR_CHECK(xTaskCreate(app_task, "app", 4096, NULL, 3, NULL) == pdPASS
                  ? ESP_OK : ESP_ERR_NO_MEM);
}
