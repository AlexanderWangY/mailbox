#include "include/queue.h"
#include "mail.h"
#include <string.h>

void amqueue_init(app_mail_queue_t *q) {
  memset(q->data, 0, sizeof(q->data));
  q->head = 0;
  q->tail = 0;
}

queue_err_t amqueue_enqueue(app_mail_queue_t *q, mail_t val) {
  if (amqueue_full(q)) return QUEUE_FULL;

  q->data[q->tail] = val;
  q->tail = (q->tail + 1) % BUFFER_SIZE;

  return QUEUE_OK;
}

queue_err_t amqueue_dequeue(app_mail_queue_t *q, mail_t *out) {
  if (amqueue_empty(q)) return QUEUE_EMPTY;

  *out = q->data[q->head];
  q->head = (q->head + 1) % BUFFER_SIZE;

  return QUEUE_OK;
}

bool amqueue_empty(const app_mail_queue_t *q) {
  return q->head == q->tail;
}

bool amqueue_full(const app_mail_queue_t *q) {
  return (q->tail + 1) % BUFFER_SIZE == q->head;
}
