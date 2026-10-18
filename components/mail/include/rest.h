#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mail.h"

typedef enum {
  MAIL_REST_POLL,
  MAIL_REST_MARK_READ,
} mail_rest_operation_t;

typedef struct {
  mail_rest_operation_t operation;
  esp_err_t error;
  bool has_mail;
  mail_t mail;
} mail_rest_result_t;

typedef void (*mail_rest_result_callback_t)(const mail_rest_result_t *result, void *context);

// Starts a worker task so TLS and HTTP work never blocks the app event loop.
esp_err_t mail_rest_init(mail_rest_result_callback_t callback, void *context);
esp_err_t mail_rest_poll_async(void);
esp_err_t mail_rest_mark_read_async(const char *mail_id);
