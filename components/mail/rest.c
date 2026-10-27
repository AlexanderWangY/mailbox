#include "rest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "board.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define REST_QUEUE_LENGTH 4
#define REST_TASK_STACK_SIZE 8192
#define REST_TASK_PRIORITY 4
#define REST_HTTP_TIMEOUT_MS 10000
#define REST_TIME_SYNC_TIMEOUT_MS 20000
#define REST_REQUEST_TIMEOUT_US (30 * 1000000LL)
#define DEVICE_TOKEN_BUFFER_SIZE 128
#define AUTH_HEADER_BUFFER_SIZE (DEVICE_TOKEN_BUFFER_SIZE + 8)
#define RESPONSE_BUFFER_SIZE 768

static const char *TAG = "mail_rest";
static const char *MAIL_NEXT_URL = "https://mailbox.awangyisu.com/api/mail/next";

typedef struct {
  mail_rest_operation_t operation;
  mail_t mail;
} rest_request_t;

typedef struct {
  char body[RESPONSE_BUFFER_SIZE];
  size_t length;
  bool overflow;
  bool cancellable;
} response_buffer_t;

static QueueHandle_t request_queue;
static mail_rest_result_callback_t result_callback;
static void *callback_context;
static TaskHandle_t worker_task;
static atomic_bool poll_canceled;
static SemaphoreHandle_t socket_lock;
static int poll_socket = -1;

static bool valid_mail_id(const char *mail_id);

static esp_err_t add_auth_header(esp_http_client_handle_t client, char *header, size_t header_size) {
  char token[DEVICE_TOKEN_BUFFER_SIZE] = {0};
  esp_err_t err = board_get_device_token(token, sizeof(token));
  if (err != ESP_OK) return err;

  int written = snprintf(header, header_size, "Bearer %s", token);
  memset(token, 0, sizeof(token));
  if (written < 0 || (size_t)written >= header_size) return ESP_ERR_INVALID_SIZE;

  return esp_http_client_set_header(client, "Authorization", header);
}

static bool request_canceled(bool cancellable) {
  return cancellable && atomic_load(&poll_canceled);
}

static esp_err_t wait_for_clock_sync(bool cancellable) {
  int64_t deadline = esp_timer_get_time() + REST_TIME_SYNC_TIMEOUT_MS * 1000LL;
  while (time(NULL) < 1704067200) {
    if (request_canceled(cancellable)) return ESP_ERR_INVALID_STATE;
    if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
    esp_netif_sntp_sync_wait(pdMS_TO_TICKS(100));
  }
  return ESP_OK;
}

static esp_err_t collect_response(esp_http_client_event_t *event) {
  response_buffer_t *response = event->user_data;
  if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
    esp_http_client_set_timeout_ms(event->client, REST_HTTP_TIMEOUT_MS);
    if (response->cancellable) {
      xSemaphoreTake(socket_lock, portMAX_DELAY);
      poll_socket = esp_http_client_get_socket(event->client);
      if (atomic_load(&poll_canceled) && poll_socket >= 0) shutdown(poll_socket, SHUT_RDWR);
      xSemaphoreGive(socket_lock);
    }
  } else if (event->event_id == HTTP_EVENT_DISCONNECTED && response->cancellable) {
    // ESP-IDF dispatches this event before closing the socket. Clear the
    // published descriptor first so cancellation cannot target a reused fd.
    xSemaphoreTake(socket_lock, portMAX_DELAY);
    poll_socket = -1;
    xSemaphoreGive(socket_lock);
  }
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  if (event->data_len < 0 || (size_t)event->data_len >= sizeof(response->body) - response->length) {
    response->overflow = true;
    return ESP_OK;
  }

  memcpy(response->body + response->length, event->data, event->data_len);
  response->length += (size_t)event->data_len;
  response->body[response->length] = '\0';
  return ESP_OK;
}

static esp_err_t perform_request(const char *url, esp_http_client_method_t method,
                                 response_buffer_t *response, int *status_code) {
  int64_t deadline = esp_timer_get_time() + REST_REQUEST_TIMEOUT_US;
  esp_err_t err = wait_for_clock_sync(response->cancellable);
  if (err != ESP_OK) return err;
  if (request_canceled(response->cancellable)) return ESP_ERR_INVALID_STATE;

  esp_http_client_config_t config = {
    .url = url,
    .method = method,
    .event_handler = collect_response,
    .user_data = response,
    .crt_bundle_attach = esp_crt_bundle_attach,
    // Yield between TCP/TLS connection steps so a background poll can stop
    // before the handshake completes. Connected reads are interrupted below
    // with shutdown(), while the worker remains the sole owner of the client.
    .is_async = true,
    .timeout_ms = 250,
    .buffer_size = 512,
    .buffer_size_tx = 256,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) return ESP_ERR_NO_MEM;

  char auth_header[AUTH_HEADER_BUFFER_SIZE];
  size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  err = add_auth_header(client, auth_header, sizeof(auth_header));
  memset(auth_header, 0, sizeof(auth_header));
  while (err == ESP_OK) {
    if (request_canceled(response->cancellable)) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
    if (esp_timer_get_time() >= deadline) {
      err = ESP_ERR_TIMEOUT;
      break;
    }
    err = esp_http_client_perform(client);
    if (err != ESP_ERR_HTTP_EAGAIN) break;
    err = ESP_OK;
    vTaskDelay(1);
  }
  if (err == ESP_OK) *status_code = esp_http_client_get_status_code(client);
  if (response->cancellable) {
    xSemaphoreTake(socket_lock, portMAX_DELAY);
    poll_socket = -1;
    xSemaphoreGive(socket_lock);
  }
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    if (!request_canceled(response->cancellable)) {
      ESP_LOGW(TAG, "HTTP request failed: %s; internal heap before TLS: free=%u largest=%u",
               esp_err_to_name(err), (unsigned)free_before, (unsigned)largest_before);
    }
    return err;
  }
  if (response->overflow) return ESP_ERR_INVALID_SIZE;
  return ESP_OK;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static esp_err_t append_utf8(char *output, size_t output_size, size_t *written, uint32_t codepoint) {
  uint8_t bytes[4];
  size_t count;
  if (codepoint == 0) return ESP_ERR_INVALID_RESPONSE;
  if (codepoint <= 0x7F) {
    bytes[0] = (uint8_t)codepoint; count = 1;
  } else if (codepoint <= 0x7FF) {
    bytes[0] = 0xC0 | (codepoint >> 6); bytes[1] = 0x80 | (codepoint & 0x3F); count = 2;
  } else if (codepoint <= 0xFFFF && (codepoint < 0xD800 || codepoint > 0xDFFF)) {
    bytes[0] = 0xE0 | (codepoint >> 12); bytes[1] = 0x80 | ((codepoint >> 6) & 0x3F);
    bytes[2] = 0x80 | (codepoint & 0x3F); count = 3;
  } else if (codepoint <= 0x10FFFF) {
    bytes[0] = 0xF0 | (codepoint >> 18); bytes[1] = 0x80 | ((codepoint >> 12) & 0x3F);
    bytes[2] = 0x80 | ((codepoint >> 6) & 0x3F); bytes[3] = 0x80 | (codepoint & 0x3F); count = 4;
  } else return ESP_ERR_INVALID_RESPONSE;
  if (*written + count >= output_size) return ESP_ERR_INVALID_SIZE;
  memcpy(output + *written, bytes, count);
  *written += count;
  return ESP_OK;
}

static esp_err_t read_json_string(const char **cursor, char *output, size_t output_size) {
  const char *p = *cursor;
  size_t written = 0;
  if (*p++ != '"') return ESP_ERR_INVALID_RESPONSE;
  while (*p != '\0' && *p != '"') {
    uint32_t codepoint;
    if ((uint8_t)*p < 0x20) return ESP_ERR_INVALID_RESPONSE;
    if (*p != '\\') {
      if (written + 1 >= output_size) return ESP_ERR_INVALID_SIZE;
      output[written++] = *p++;
      continue;
    }
    p++;
    switch (*p++) {
      case '"': codepoint = '"'; break;
      case '\\': codepoint = '\\'; break;
      case '/': codepoint = '/'; break;
      case 'b': codepoint = '\b'; break;
      case 'f': codepoint = '\f'; break;
      case 'n': codepoint = '\n'; break;
      case 'r': codepoint = '\r'; break;
      case 't': codepoint = '\t'; break;
      case 'u': {
        codepoint = 0;
        for (int i = 0; i < 4; i++) {
          int digit = hex_value(*p++);
          if (digit < 0) return ESP_ERR_INVALID_RESPONSE;
          codepoint = (codepoint << 4) | (uint32_t)digit;
        }
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
          if (p[0] != '\\' || p[1] != 'u') return ESP_ERR_INVALID_RESPONSE;
          p += 2;
          uint32_t low = 0;
          for (int i = 0; i < 4; i++) {
            int digit = hex_value(*p++);
            if (digit < 0) return ESP_ERR_INVALID_RESPONSE;
            low = (low << 4) | (uint32_t)digit;
          }
          if (low < 0xDC00 || low > 0xDFFF) return ESP_ERR_INVALID_RESPONSE;
          codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        }
        break;
      }
      default: return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t err = append_utf8(output, output_size, &written, codepoint);
    if (err != ESP_OK) return err;
  }
  if (*p != '"') return ESP_ERR_INVALID_RESPONSE;
  output[written] = '\0';
  *cursor = p + 1;
  return ESP_OK;
}

static esp_err_t json_string_field(const response_buffer_t *response, const char *name,
                                   char *output, size_t output_size) {
  char key[24];
  int key_length = snprintf(key, sizeof(key), "\"%s\"", name);
  if (key_length < 0 || (size_t)key_length >= sizeof(key)) return ESP_ERR_INVALID_ARG;
  const char *p = strstr(response->body, key);
  if (p == NULL || p >= response->body + response->length) return ESP_ERR_INVALID_RESPONSE;
  p += key_length;
  while (p < response->body + response->length && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if (p >= response->body + response->length || *p++ != ':') return ESP_ERR_INVALID_RESPONSE;
  while (p < response->body + response->length && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  return read_json_string(&p, output, output_size);
}

static esp_err_t parse_mail_response(const response_buffer_t *response, mail_t *mail) {
  esp_err_t err = json_string_field(response, "id", mail->id, sizeof(mail->id));
  if (err != ESP_OK) return err;
  err = json_string_field(response, "content", mail->content, sizeof(mail->content));
  if (err != ESP_OK) return err;
  return valid_mail_id(mail->id) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t poll_mail(mail_t *mail, bool *has_mail) {
  response_buffer_t response = {.cancellable = true};
  int status_code = 0;
  esp_err_t err = perform_request(MAIL_NEXT_URL, HTTP_METHOD_GET, &response, &status_code);
  if (err != ESP_OK) return err;

  if (status_code == 204) {
    *has_mail = false;
    return ESP_OK;
  }
  if (status_code == HttpStatus_Unauthorized) return ESP_ERR_INVALID_STATE;
  if (status_code != HttpStatus_Ok) {
    ESP_LOGW(TAG, "Mail poll returned HTTP %d", status_code);
    return ESP_FAIL;
  }

  err = parse_mail_response(&response, mail);
  if (err == ESP_OK) *has_mail = true;
  return err;
}

static bool valid_mail_id(const char *mail_id) {
  if (mail_id == NULL || strlen(mail_id) != 36) return false;
  for (size_t i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (mail_id[i] != '-') return false;
    } else if (!((mail_id[i] >= '0' && mail_id[i] <= '9') ||
                 (mail_id[i] >= 'a' && mail_id[i] <= 'f') ||
                 (mail_id[i] >= 'A' && mail_id[i] <= 'F'))) {
      return false;
    }
  }
  return true;
}

static esp_err_t mark_mail_read(const char *mail_id) {
  if (!valid_mail_id(mail_id)) return ESP_ERR_INVALID_ARG;

  char url[sizeof("https://mailbox.awangyisu.com/api/mail/") + 36 + sizeof("/read")];
  int written = snprintf(url, sizeof(url), "https://mailbox.awangyisu.com/api/mail/%s/read", mail_id);
  if (written < 0 || (size_t)written >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

  response_buffer_t response = {0};
  int status_code = 0;
  esp_err_t err = perform_request(url, HTTP_METHOD_POST, &response, &status_code);
  if (err != ESP_OK) return err;
  if (status_code == HttpStatus_Ok) return ESP_OK;
  if (status_code == HttpStatus_Unauthorized) return ESP_ERR_INVALID_STATE;
  if (status_code == HttpStatus_NotFound) return ESP_ERR_NOT_FOUND;
  ESP_LOGW(TAG, "Mark read returned HTTP %d", status_code);
  return ESP_FAIL;
}

static void mail_rest_worker(void *arg) {
  (void)arg;
  rest_request_t request;

  for (;;) {
    if (xQueueReceive(request_queue, &request, portMAX_DELAY) != pdTRUE) continue;

    mail_rest_result_t result = {
      .operation = request.operation,
      .mail = request.mail,
    };
    if (request.operation == MAIL_REST_POLL) {
      result.error = poll_mail(&result.mail, &result.has_mail);
    } else {
      result.error = mark_mail_read(request.mail.id);
    }
    if (request.operation == MAIL_REST_POLL && atomic_load(&poll_canceled)) {
      result.status = MAIL_REST_CANCELED;
    } else {
      result.status = result.error == ESP_OK ? MAIL_REST_SUCCEEDED : MAIL_REST_FAILED;
    }
    if (result_callback != NULL) result_callback(&result, callback_context);
  }
}

esp_err_t mail_rest_init(mail_rest_result_callback_t callback, void *context) {
  if (callback == NULL) return ESP_ERR_INVALID_ARG;
  if (request_queue != NULL) return ESP_ERR_INVALID_STATE;

  request_queue = xQueueCreate(REST_QUEUE_LENGTH, sizeof(rest_request_t));
  if (request_queue == NULL) return ESP_ERR_NO_MEM;
  socket_lock = xSemaphoreCreateMutex();
  if (socket_lock == NULL) {
    vQueueDelete(request_queue);
    request_queue = NULL;
    return ESP_ERR_NO_MEM;
  }
  result_callback = callback;
  callback_context = context;

  BaseType_t created = xTaskCreate(mail_rest_worker, "mail rest", REST_TASK_STACK_SIZE,
                                   NULL, REST_TASK_PRIORITY, &worker_task);
  if (created != pdPASS) {
    vQueueDelete(request_queue);
    vSemaphoreDelete(socket_lock);
    request_queue = NULL;
    result_callback = NULL;
    callback_context = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t mail_rest_poll_async(void) {
  if (request_queue == NULL) return ESP_ERR_INVALID_STATE;
  atomic_store(&poll_canceled, false);
  rest_request_t request = {.operation = MAIL_REST_POLL};
  return xQueueSend(request_queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void mail_rest_cancel_poll(void) {
  atomic_store(&poll_canceled, true);
  xSemaphoreTake(socket_lock, portMAX_DELAY);
  // shutdown interrupts a pending read without freeing the client's resources.
  if (poll_socket >= 0) shutdown(poll_socket, SHUT_RDWR);
  xSemaphoreGive(socket_lock);
}

esp_err_t mail_rest_mark_read_async(const mail_t *mail) {
  if (request_queue == NULL) return ESP_ERR_INVALID_STATE;
  if (mail == NULL || !valid_mail_id(mail->id)) return ESP_ERR_INVALID_ARG;
  rest_request_t request = {.operation = MAIL_REST_MARK_READ, .mail = *mail};
  return xQueueSend(request_queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
