#include "test_platform.h"
#include "../components/app/power.c"

static int64_t now;
static uint64_t sleep_delay;
static bool pending_touch, input_restored, screen_lit, consumed;
static unsigned sleeps, restarts;
static uint32_t wake_causes;
static esp_err_t stop_error, sleep_error;

int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
esp_err_t board_wifi_stop_for_sleep(void) { now += 200; return stop_error; }
esp_err_t board_wifi_resume_after_sleep(void) {
  assert(input_restored);
  if (pending_touch || wake_causes == (1U << ESP_SLEEP_WAKEUP_GPIO) || sleep_error != ESP_OK) {
    assert(screen_lit); // Slow Wi-Fi startup never delays lighting the screen.
  }
  restarts++;
  return ESP_OK;
}
bool touch_prepare_sleep(uint32_t sequence) { assert(sequence == 5); return !pending_touch; }
uint32_t touch_finish_sleep(bool consume) {
  input_restored = true;
  consumed = consume;
  return 6;
}
void display_wake(void) { screen_lit = true; }
void display_normal(void) { screen_lit = true; }
void display_dim(void) {}
void display_sleep(void) { screen_lit = false; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t delay) { sleep_delay = delay; return ESP_OK; }
esp_err_t esp_light_sleep_start(void) { sleeps++; return sleep_error; }
uint32_t esp_sleep_get_wakeup_causes(void) { return wake_causes; }

static void reset(void) {
  now = 100;
  sleep_delay = sleeps = restarts = 0;
  pending_touch = input_restored = screen_lit = consumed = false;
  wake_causes = 1U << ESP_SLEEP_WAKEUP_TIMER;
  stop_error = sleep_error = ESP_OK;
}

int main(void) {
  uint32_t sequence = 5;
  reset();
  assert(power_sleep_until(1000, &sequence) == POWER_WAKE_TIMER);
  assert(sleep_delay == 700 && sleeps == 1 && restarts == 1);
  assert(!screen_lit && !consumed && sequence == 5);

  reset();
  wake_causes = 1U << ESP_SLEEP_WAKEUP_GPIO;
  assert(power_sleep_until(1000, &sequence) == POWER_WAKE_TOUCH);
  assert(screen_lit && consumed && input_restored && restarts == 1 && sequence == 6);

  reset();
  sequence = 5;
  pending_touch = true;
  assert(power_sleep_until(1000, &sequence) == POWER_WAKE_TOUCH);
  assert(sleeps == 0 && consumed && screen_lit && restarts == 1);

  reset();
  sequence = 5;
  sleep_error = ESP_FAIL;
  assert(power_sleep_until(1000, &sequence) == POWER_WAKE_ERROR);
  assert(input_restored && screen_lit && restarts == 1);

  reset();
  stop_error = ESP_FAIL;
  assert(power_sleep_until(1000, &sequence) == POWER_WAKE_ERROR);
  assert(sleeps == 0 && restarts == 0 && screen_lit);
  puts("Sleep transition and wake ordering tests passed");
}
