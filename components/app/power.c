#include "power.h"

#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "touch.h"

static const char *TAG = "power";

void power_display_transition(app_power_t from, app_power_t to) {
  if (from == to) return;
  switch (to) {
  case APP_POWER_AWAKE:
    if (from == APP_POWER_OFF) display_wake();
    else display_normal();
    break;
  case APP_POWER_DIMMED:
    display_dim();
    break;
  case APP_POWER_OFF:
    display_sleep();
    break;
  }
}

power_wake_t power_sleep_until(int64_t deadline_us, uint32_t *consumed_touch) {
  power_wake_t reason = POWER_WAKE_ERROR;
  esp_err_t err = board_wifi_stop_for_sleep();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Cannot stop Wi-Fi for sleep: %s", esp_err_to_name(err));
    display_wake();
    return POWER_WAKE_ERROR;
  } else if (!touch_prepare_sleep(*consumed_touch)) {
    reason = POWER_WAKE_TOUCH;
    *consumed_touch = touch_finish_sleep(true);
  } else {
    int64_t delay_us = deadline_us - esp_timer_get_time();
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(delay_us > 0 ? delay_us : 1));
    err = esp_light_sleep_start();
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (err == ESP_OK) {
      reason = (causes & (1U << ESP_SLEEP_WAKEUP_GPIO))
        ? POWER_WAKE_TOUCH : POWER_WAKE_TIMER;
      ESP_LOGI(TAG, "Wake: %s", reason == POWER_WAKE_TOUCH ? "touch" : "mail timer");
    } else {
      ESP_LOGW(TAG, "Light sleep failed: %s", esp_err_to_name(err));
    }
    uint32_t wake_sequence = touch_finish_sleep(reason == POWER_WAKE_TOUCH);
    if (reason == POWER_WAKE_TOUCH) *consumed_touch = wake_sequence;
  }

  if (reason != POWER_WAKE_TIMER) display_wake();
  err = board_wifi_resume_after_sleep();
  if (err != ESP_OK) ESP_LOGW(TAG, "Cannot restart Wi-Fi: %s", esp_err_to_name(err));
  return reason;
}
