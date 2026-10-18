#pragma once

#include "portmacro.h"

typedef enum {
    // User interaction
    APP_EVENT_TOUCH_TAP,
    APP_EVENT_MOTION,

    // Mail
    APP_EVENT_NEW_MAIL,
    APP_EVENT_MAIL_POLL_DUE,

    // Power management
    APP_EVENT_IDLE_DIM,
    APP_EVENT_IDLE_SLEEP,

} app_event_type_t;

typedef struct {
  app_event_type_t type;

  union {};
} app_event_t;


void app_init(void);

BaseType_t app_create_event(const app_event_t *event);

void app_task(void *arg);
