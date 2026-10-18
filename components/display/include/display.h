#pragma once

#define LCD_WIDTH 480
#define LCD_HEIGHT 320

#define LCD_TRANSFER_LINES 40

typedef enum {
  DISPLAY_NORMAL,
  DISPLAY_DIM,
  DISPLAY_SLEEP,
} display_state_t;

void display_init(void);
void display_sleep(void);
void display_wake(void);
void display_dim(void);
void display_normal(void);

void display_clear(void);

display_state_t display_state(void);
