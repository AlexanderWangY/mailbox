#pragma once

#include <stdint.h>

#include "state.h"

typedef enum {
  POWER_WAKE_TOUCH,
  POWER_WAKE_TIMER,
  POWER_WAKE_ERROR,
} power_wake_t;

void power_display_transition(app_power_t from, app_power_t to);

// Called only with the display off and no network request in flight.
// A touch returns with the screen lit before Wi-Fi is restarted.
power_wake_t power_sleep_until(int64_t deadline_us, uint32_t *consumed_touch);
