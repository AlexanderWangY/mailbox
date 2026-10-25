#include <string.h>

#include "test_platform.h"
#include "../components/app/app.c"

static int64_t now;
static bool connected;
static unsigned polls, cancellations, reads;
static esp_err_t read_queue_result;
static app_power_t displayed_power;
static app_screen_t displayed_screen;
static mail_t marked_mail;

int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
bool board_wifi_started(void) { return true; }
bool board_wifi_connected(void) { return connected; }
esp_err_t board_battery_read_percent(int *percent) { *percent = 50; return ESP_OK; }
void ui_draw_empty_screen(void) { displayed_screen = APP_SCREEN_EMPTY; }
void ui_draw_mail_screen(void) { displayed_screen = APP_SCREEN_MAIL; }
void ui_draw_reading_screen(void) { displayed_screen = APP_SCREEN_READING; }
void ui_set_reading_message(const char *text) { (void)text; }
void ui_set_battery_status(int percent, bool charging) { (void)percent; (void)charging; }
void power_display_transition(app_power_t from, app_power_t to) {
  (void)from;
  displayed_power = to;
}
esp_err_t mail_rest_poll_async(void) { polls++; return ESP_OK; }
void mail_rest_cancel_poll(void) {
  assert(displayed_power == APP_POWER_AWAKE); // Wake does not wait for HTTP.
  cancellations++;
}
esp_err_t mail_rest_mark_read_async(const mail_t *mail) {
  if (read_queue_result != ESP_OK) return read_queue_result;
  reads++;
  marked_mail = *mail;
  return ESP_OK;
}
esp_err_t haptic_play(uint8_t effect) { (void)effect; return ESP_OK; }

static void reset(void) {
  now = 0;
  connected = true;
  polls = cancellations = reads = 0;
  read_queue_result = ESP_OK;
  displayed_power = APP_POWER_AWAKE;
  displayed_screen = APP_SCREEN_EMPTY;
  app_state_init(&app, now);
}

static void poll_complete(const mail_t *mail) {
  mail_rest_result_t result = {
    .operation = MAIL_REST_POLL,
    .status = MAIL_REST_SUCCEEDED,
    .has_mail = mail != NULL,
  };
  if (mail != NULL) result.mail = *mail;
  handle_mail(&result, now);
}

static mail_t message(unsigned id) {
  mail_t mail = {0};
  snprintf(mail.id, sizeof(mail.id), "message-%u", id);
  snprintf(mail.content, sizeof(mail.content), "Message %u", id);
  return mail;
}

static void test_deadlines(void) {
  reset();
  service_deadlines(now);
  assert(polls == 1 && app.poll == APP_POLL_RUNNING);
  assert(app.next_poll_us == APP_POLL_US);
  poll_complete(NULL);
  now = APP_DIM_US - 1;
  service_deadlines(now);
  assert(app.power == APP_POWER_AWAKE);
  now++;
  service_deadlines(now);
  assert(app.power == APP_POWER_DIMMED);
  assert(app_state_deadline(&app) == APP_SLEEP_US);
  now = APP_SLEEP_US - 1;
  service_deadlines(now);
  assert(app.power == APP_POWER_DIMMED);
  now++;
  service_deadlines(now);
  assert(app.power == APP_POWER_OFF && app_state_can_sleep(&app));
  assert(app_state_deadline(&app) == APP_POLL_US);
  now = APP_POLL_US;
  service_deadlines(now);
  assert(polls == 2 && app.poll == APP_POLL_RUNNING);
  assert(app.power == APP_POWER_OFF);
  assert(app.next_poll_us == 2 * APP_POLL_US);
  poll_complete(NULL);
  assert(app_state_can_sleep(&app)); // No extra 45 seconds after background work.
}

static void test_background_tap(void) {
  reset();
  now = APP_SLEEP_US;
  app.power = APP_POWER_OFF;
  service_deadlines(now);
  assert(polls == 1);
  handle_touch(1, now);
  assert(app.power == APP_POWER_AWAKE && cancellations == 1);
  assert(app.poll == APP_POLL_CANCELING);
  service_deadlines(now);
  assert(polls == 1); // Never overlap the old and replacement requests.
  mail_t stale = message(1);
  poll_complete(&stale);
  assert(app.mail_count == 0 && app.poll == APP_POLL_IDLE);
  service_deadlines(now);
  assert(polls == 2 && app.poll == APP_POLL_RUNNING);
  mail_t fresh = message(2);
  poll_complete(&fresh);
  assert(app.screen == APP_SCREEN_MAIL && app.mail_count == 1);
  assert(strcmp(app.inbox[0].id, fresh.id) == 0);
}

static void test_mail_navigation(void) {
  reset();
  mail_t first = message(1), second = message(2);
  assert(app_state_receive(&app, &first));
  assert(app_state_receive(&app, &second));
  render();
  assert(displayed_screen == APP_SCREEN_MAIL);
  handle_touch(1, now);
  assert(displayed_screen == APP_SCREEN_READING);
  handle_touch(2, now);
  assert(reads == 1 && app.pending_reads == 1);
  assert(strcmp(marked_mail.id, first.id) == 0);
  assert(displayed_screen == APP_SCREEN_MAIL && app.mail_count == 1);
  handle_touch(3, now);
  assert(displayed_screen == APP_SCREEN_READING);
  handle_touch(4, now);
  assert(displayed_screen == APP_SCREEN_EMPTY && app.mail_count == 0);
  assert(app.pending_reads == 2);
  app.power = APP_POWER_OFF;
  assert(!app_state_can_sleep(&app));
  mail_rest_result_t ack = {.operation = MAIL_REST_MARK_READ, .status = MAIL_REST_SUCCEEDED};
  handle_mail(&ack, now);
  handle_mail(&ack, now);
  assert(app.pending_reads == 0);
}

static void test_failed_read(void) {
  reset();
  mail_t mail = message(1);
  app_state_receive(&app, &mail);
  handle_touch(1, now);
  read_queue_result = ESP_ERR_TIMEOUT;
  handle_touch(2, now);
  assert(app.screen == APP_SCREEN_READING && app.mail_count == 1 && app.pending_reads == 0);
  read_queue_result = ESP_OK;
  handle_touch(3, now);
  mail_rest_result_t failure = {
    .operation = MAIL_REST_MARK_READ, .status = MAIL_REST_FAILED, .mail = mail,
  };
  handle_mail(&failure, now);
  assert(app.screen == APP_SCREEN_MAIL && app.mail_count == 1 && app.pending_reads == 0);
}

static void test_background_delivery(void) {
  reset();
  now = APP_SLEEP_US;
  app.power = APP_POWER_OFF;
  service_deadlines(now);
  mail_t first = message(1), second = message(2);
  poll_complete(&first);
  assert(app.power == APP_POWER_AWAKE && displayed_power == APP_POWER_AWAKE);
  assert(displayed_screen == APP_SCREEN_MAIL && app.last_activity_us == now);
  handle_touch(1, now);
  app.poll = APP_POLL_RUNNING;
  poll_complete(&second);
  assert(app.screen == APP_SCREEN_READING && app.mail_count == 2);
  assert(strcmp(app.inbox[0].id, first.id) == 0);
}

static void test_wake_consumes_only_wake_press(void) {
  reset();
  mail_t mail = message(1);
  app_state_receive(&app, &mail);
  app.power = APP_POWER_DIMMED;
  handle_touch(1, now);
  assert(app.screen == APP_SCREEN_MAIL && app.power == APP_POWER_AWAKE);
  handle_touch(2, now); // A distinct tap is accepted immediately, without a 500 ms window.
  assert(app.screen == APP_SCREEN_READING);
  app.screen = APP_SCREEN_MAIL;
  app.consumed_touch = 3; // The power module already consumed this wake press.
  handle_touch(3, now);
  assert(app.screen == APP_SCREEN_MAIL);
  handle_touch(4, now);
  assert(app.screen == APP_SCREEN_READING);
}

static void test_connection_and_capacity(void) {
  reset();
  connected = false;
  now = APP_SLEEP_US;
  app.power = APP_POWER_OFF;
  service_deadlines(now);
  assert(app.poll == APP_POLL_WAITING_FOR_WIFI && !app_state_can_sleep(&app));
  handle_touch(1, now);
  assert(app.power == APP_POWER_AWAKE && cancellations == 0);
  connected = true;
  service_deadlines(now);
  assert(polls == 1 && app.poll == APP_POLL_RUNNING);

  reset();
  connected = false;
  service_deadlines(now);
  now = APP_CONNECT_US;
  service_deadlines(now);
  assert(app.poll == APP_POLL_IDLE && app.next_poll_us == now + APP_POLL_US);
  connected = true;
  service_deadlines(now);
  assert(polls == 0); // Reconnecting alone does not trigger an extra poll.

  reset();
  for (unsigned i = 0; i < APP_MAIL_CAPACITY; i++) {
    mail_t mail = message(i);
    assert(app_state_receive(&app, &mail));
  }
  mail_t duplicate = message(0), overflow = message(APP_MAIL_CAPACITY);
  assert(!app_state_receive(&app, &duplicate));
  assert(!app_state_receive(&app, &overflow));
  service_deadlines(now);
  assert(polls == 0);
}

int main(void) {
  test_deadlines();
  test_background_tap();
  test_mail_navigation();
  test_failed_read();
  test_background_delivery();
  test_wake_consumes_only_wake_press();
  test_connection_and_capacity();
  puts("App transition and deadline tests passed");
}
