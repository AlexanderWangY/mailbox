#include "state.h"

#include <limits.h>
#include <string.h>

void app_state_init(app_state_t *state, int64_t now_us) {
  *state = (app_state_t){
    .screen = APP_SCREEN_EMPTY,
    .power = APP_POWER_AWAKE,
    .poll = APP_POLL_IDLE,
    .last_activity_us = now_us,
    .next_poll_us = now_us,
    .next_battery_us = now_us,
  };
}

void app_state_wake(app_state_t *state, int64_t now_us) {
  state->power = APP_POWER_AWAKE;
  state->last_activity_us = now_us;
}

app_power_t app_state_power_due(const app_state_t *state, int64_t now_us) {
  int64_t idle_us = now_us - state->last_activity_us;
  if (idle_us >= APP_SLEEP_US) return APP_POWER_OFF;
  if (idle_us >= APP_DIM_US) return APP_POWER_DIMMED;
  return APP_POWER_AWAKE;
}

bool app_state_can_sleep(const app_state_t *state) {
  return state->power == APP_POWER_OFF && state->poll == APP_POLL_IDLE &&
         state->pending_reads == 0;
}

int64_t app_state_deadline(const app_state_t *state) {
  int64_t deadline = INT64_MAX;
  if (state->power != APP_POWER_OFF) {
    deadline = state->last_activity_us +
      (state->power == APP_POWER_AWAKE ? APP_DIM_US : APP_SLEEP_US);
    if (state->next_battery_us < deadline) deadline = state->next_battery_us;
  }
  if (state->poll == APP_POLL_IDLE && state->pending_reads == 0 &&
      state->next_poll_us < deadline) {
    deadline = state->next_poll_us;
  }
  if (state->poll == APP_POLL_WAITING_FOR_WIFI && state->connect_deadline_us < deadline) {
    deadline = state->connect_deadline_us;
  }
  return deadline;
}

bool app_state_receive(app_state_t *state, const mail_t *mail) {
  for (size_t i = 0; i < state->mail_count; i++) {
    if (strcmp(state->inbox[i].id, mail->id) == 0) return false;
  }
  if (state->mail_count == APP_MAIL_CAPACITY) return false;
  state->inbox[state->mail_count++] = *mail;
  if (state->screen == APP_SCREEN_EMPTY) state->screen = APP_SCREEN_MAIL;
  return true;
}

void app_state_open(app_state_t *state) {
  if (state->screen == APP_SCREEN_MAIL) state->screen = APP_SCREEN_READING;
}

void app_state_dismiss(app_state_t *state) {
  if (state->screen != APP_SCREEN_READING) return;
  state->mail_count--;
  memmove(state->inbox, state->inbox + 1, state->mail_count * sizeof(mail_t));
  state->screen = state->mail_count == 0 ? APP_SCREEN_EMPTY : APP_SCREEN_MAIL;
}
