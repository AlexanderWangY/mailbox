#pragma once
#include <stdint.h>

void touch_init(void);
void touch_read(void);

void touch_task(void *arg);
uint32_t touch_interrupt_count(void);
uint32_t touch_tap_count(void);
