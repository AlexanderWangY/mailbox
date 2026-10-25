#pragma once

#include "test_platform.h"

typedef struct fake_client *esp_http_client_handle_t;
typedef enum { HTTP_METHOD_GET, HTTP_METHOD_POST } esp_http_client_method_t;
enum { HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_DISCONNECTED, HTTP_EVENT_ON_DATA };
enum { HttpStatus_Ok = 200, HttpStatus_Unauthorized = 401, HttpStatus_NotFound = 404 };
typedef struct {
  int event_id;
  esp_http_client_handle_t client;
  void *user_data;
  int data_len;
  char *data;
} esp_http_client_event_t;
typedef struct {
  const char *url;
  esp_http_client_method_t method;
  esp_err_t (*event_handler)(esp_http_client_event_t *);
  void *user_data;
  esp_err_t (*crt_bundle_attach)(void *);
  bool is_async;
  int timeout_ms;
  int buffer_size;
  int buffer_size_tx;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int ms);
int esp_http_client_get_socket(esp_http_client_handle_t client);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_crt_bundle_attach(void *config);
esp_err_t esp_netif_sntp_sync_wait(TickType_t wait);
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 10000; }
static inline size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 10000; }
#define SHUT_RDWR 2
#define shutdown test_shutdown
int test_shutdown(int fd, int how);
