#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mail.h"

#define APP_MAIL_CAPACITY 5
#define APP_DIM_US (30 * 1000000LL)
#define APP_SLEEP_US (45 * 1000000LL)
#define APP_POLL_US (10 * 60 * 1000000LL)
#define APP_BATTERY_US (60 * 1000000LL)
#define APP_CONNECT_US (30 * 1000000LL)

typedef enum {
  APP_SCREEN_EMPTY,
  APP_SCREEN_MAIL,
  APP_SCREEN_READING,
} app_screen_t;

typedef enum {
  APP_POWER_AWAKE,
  APP_POWER_DIMMED,
  APP_POWER_OFF,
} app_power_t;

typedef enum {
  APP_POLL_IDLE,
  APP_POLL_WAITING_FOR_WIFI,
  APP_POLL_RUNNING,
  APP_POLL_CANCELING,
} app_poll_t;

typedef struct {
  app_screen_t screen;
  app_power_t power;
  app_poll_t poll;
  mail_t inbox[APP_MAIL_CAPACITY];
  size_t mail_count;
  size_t pending_reads;
  uint32_t consumed_touch;
  int64_t last_activity_us;
  int64_t next_poll_us;
  int64_t next_battery_us;
  int64_t connect_deadline_us;
} app_state_t;

void app_state_init(app_state_t *state, int64_t now_us);
void app_state_wake(app_state_t *state, int64_t now_us);
app_power_t app_state_power_due(const app_state_t *state, int64_t now_us);
int64_t app_state_deadline(const app_state_t *state);
bool app_state_can_sleep(const app_state_t *state);
bool app_state_receive(app_state_t *state, const mail_t *mail);
void app_state_open(app_state_t *state);
void app_state_dismiss(app_state_t *state);
