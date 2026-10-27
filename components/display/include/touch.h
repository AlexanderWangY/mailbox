#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*touch_callback_t)(uint32_t sequence, void *context);

void touch_init(touch_callback_t callback, void *context);

// Refuses sleep when a confirmed tap has not yet been consumed by the app.
bool touch_prepare_sleep(uint32_t consumed_sequence);
uint32_t touch_finish_sleep(bool consume_wake_press);
