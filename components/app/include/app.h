#pragma once

#include "mail.h"
#include "portmacro.h"
#include <stdint.h>

typedef enum {
    // User interaction
    APP_EVENT_TOUCH_TAP,

    // Mail
    APP_EVENT_NEW_MAIL,
    APP_EVENT_MAIL_POLL_DUE,
    APP_EVENT_MAIL_POLL_COMPLETE,
    APP_EVENT_MAIL_POLL_FAILED,
    APP_EVENT_MAIL_POLL_TIMEOUT,
    APP_EVENT_MAIL_READ_COMPLETE,
    APP_EVENT_MAIL_READ_FAILED,

    // Power management
    APP_EVENT_IDLE_DIM,
    APP_EVENT_IDLE_SLEEP,

} app_event_type_t;

typedef struct {
  app_event_type_t type;

  union {
    mail_t mail_content;
    uint32_t touch_tick;
  };
} app_event_t;


void app_init(void);

BaseType_t app_create_event(const app_event_t *event);

void app_task(void *arg);
