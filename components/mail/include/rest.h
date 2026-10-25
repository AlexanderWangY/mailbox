#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mail.h"

typedef enum {
  MAIL_REST_POLL,
  MAIL_REST_MARK_READ,
} mail_rest_operation_t;

typedef enum {
  MAIL_REST_SUCCEEDED,
  MAIL_REST_FAILED,
  MAIL_REST_CANCELED,
} mail_rest_status_t;

typedef struct {
  mail_rest_operation_t operation;
  mail_rest_status_t status;
  esp_err_t error;
  bool has_mail;
  mail_t mail;
} mail_rest_result_t;

typedef void (*mail_rest_result_callback_t)(const mail_rest_result_t *result, void *context);

// Starts a worker task so TLS and HTTP work never blocks the app event loop.
esp_err_t mail_rest_init(mail_rest_result_callback_t callback, void *context);
// At most one poll may be pending. Cancellation still delivers one completion;
// wait for it before starting the replacement request.
esp_err_t mail_rest_poll_async(void);
void mail_rest_cancel_poll(void);
esp_err_t mail_rest_mark_read_async(const mail_t *mail);
