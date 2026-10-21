#pragma once

#include "mail.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define QUEUE_SIZE 4
#define BUFFER_SIZE (QUEUE_SIZE + 1)

typedef enum {
  QUEUE_OK,
  QUEUE_FULL,
  QUEUE_EMPTY,
} queue_err_t;

typedef struct {
  mail_t data[BUFFER_SIZE];
  size_t head;
  size_t tail;
} app_mail_queue_t;

void amqueue_init(app_mail_queue_t *q);
queue_err_t amqueue_enqueue(app_mail_queue_t *q, mail_t val);
queue_err_t amqueue_dequeue(app_mail_queue_t *q, mail_t *out);

bool amqueue_empty(const app_mail_queue_t *q);
bool amqueue_full(const app_mail_queue_t *q);
