#include <string.h>
#include <time.h>

#include "test_http_platform.h"

static time_t test_clock(time_t *out);
#define time test_clock
#include "../components/mail/rest.c"
#undef time

typedef enum {
  TEST_EMPTY,
  TEST_CANCEL_CONNECT,
  TEST_CANCEL_READ,
  TEST_CANCEL_AFTER_CLOSE,
  TEST_DEADLINE,
  TEST_CANCEL_CLOCK,
} test_mode_t;

struct fake_client { esp_http_client_config_t config; };
static struct fake_client client;
static test_mode_t test_mode;
static int64_t now;
static time_t clock_value;
static unsigned performs, cleanups, shutdowns, lock_depth;

static time_t test_clock(time_t *out) { if (out) *out = clock_value; return clock_value; }
int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
esp_err_t board_get_device_token(char *token, size_t size) {
  snprintf(token, size, "test-token");
  return ESP_OK;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
  (void)mutex; (void)wait;
  assert(lock_depth++ == 0);
  return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
  (void)mutex;
  assert(lock_depth-- == 1);
  return pdTRUE;
}
void vTaskDelay(TickType_t ticks) { now += ticks * 10000LL; }
esp_err_t esp_netif_sntp_sync_wait(TickType_t wait) {
  now += wait * 10000LL;
  if (test_mode == TEST_CANCEL_CLOCK) mail_rest_cancel_poll();
  return ESP_ERR_TIMEOUT;
}
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  assert(config->is_async && config->timeout_ms == 250);
  client.config = *config;
  return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value) {
  (void)c; (void)key; (void)value;
  return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int ms) {
  (void)c;
  assert(ms == REST_HTTP_TIMEOUT_MS);
  return ESP_OK;
}
int esp_http_client_get_socket(esp_http_client_handle_t c) { (void)c; return 123; }
int test_shutdown(int fd, int how) {
  assert(fd == 123 && how == SHUT_RDWR && lock_depth == 1);
  shutdowns++;
  return 0;
}
static void http_event(int id) {
  esp_http_client_event_t event = {
    .event_id = id, .client = &client, .user_data = client.config.user_data,
  };
  assert(client.config.event_handler(&event) == ESP_OK);
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t c) {
  (void)c;
  performs++;
  if (test_mode == TEST_CANCEL_CONNECT) {
    mail_rest_cancel_poll();
    return ESP_ERR_HTTP_EAGAIN;
  }
  if (test_mode == TEST_DEADLINE) {
    now += REST_REQUEST_TIMEOUT_US;
    return ESP_ERR_HTTP_EAGAIN;
  }
  http_event(HTTP_EVENT_ON_CONNECTED);
  if (test_mode == TEST_CANCEL_READ) {
    assert(poll_socket == 123);
    mail_rest_cancel_poll();
    assert(shutdowns == 1);
    return ESP_FAIL;
  }
  if (test_mode == TEST_CANCEL_AFTER_CLOSE) {
    http_event(HTTP_EVENT_DISCONNECTED);
    assert(poll_socket == -1);
    mail_rest_cancel_poll();
    assert(shutdowns == 0);
    return ESP_FAIL;
  }
  return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return 204; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) {
  (void)c;
  assert(poll_socket == -1); // Cancellation loses access before the fd can be reused.
  http_event(HTTP_EVENT_DISCONNECTED);
  cleanups++;
  return ESP_OK;
}

static void reset(test_mode_t mode) {
  test_mode = mode;
  now = 0;
  clock_value = mode == TEST_CANCEL_CLOCK ? 0 : 1800000000;
  performs = cleanups = shutdowns = lock_depth = 0;
  poll_socket = -1;
  socket_lock = (void *)1;
  atomic_store(&poll_canceled, false);
}

int main(void) {
  mail_t mail;
  bool has_mail = false;
  reset(TEST_EMPTY);
  assert(poll_mail(&mail, &has_mail) == ESP_OK && !has_mail);
  assert(performs == 1 && cleanups == 1 && shutdowns == 0);

  reset(TEST_CANCEL_CONNECT);
  assert(poll_mail(&mail, &has_mail) != ESP_OK);
  assert(atomic_load(&poll_canceled) && performs == 1 && cleanups == 1 && shutdowns == 0);

  reset(TEST_CANCEL_READ);
  assert(poll_mail(&mail, &has_mail) != ESP_OK);
  assert(atomic_load(&poll_canceled) && cleanups == 1 && shutdowns == 1);

  reset(TEST_CANCEL_AFTER_CLOSE);
  assert(poll_mail(&mail, &has_mail) != ESP_OK);
  assert(cleanups == 1 && shutdowns == 0);

  reset(TEST_DEADLINE);
  assert(poll_mail(&mail, &has_mail) == ESP_ERR_TIMEOUT && performs == 1 && cleanups == 1);

  reset(TEST_CANCEL_CLOCK);
  assert(poll_mail(&mail, &has_mail) != ESP_OK);
  assert(atomic_load(&poll_canceled) && performs == 0 && cleanups == 0);
  puts("REST cancellation and socket lifetime tests passed");
}
