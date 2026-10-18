#pragma once

#include "portmacro.h"
#include "ui.h"
#define LCD_WIDTH 480
#define LCD_HEIGHT 320

#define LCD_TRANSFER_LINES 40

typedef enum {
  DISPLAY_NORMAL,
  DISPLAY_DIM,
  DISPLAY_SLEEP,
} display_state_t;

typedef enum {
  DISPLAY_CMD_SET_SCREEN,
  DISPLAY_CMD_SET_MESSAGE,
} display_cmd_type_t;

typedef struct {
  display_cmd_type_t type;

  union {
    ui_screen_t screen;

    struct {
      char text[1024];
    } message;
  } data;
} display_cmd_t;

void display_init(void);
void display_sleep(void);
void display_wake(void);
void display_dim(void);
void display_normal(void);

void display_clear(void);

display_state_t display_state(void);

BaseType_t display_submit_cmd(const display_cmd_t *cmd);

void display_task(void *arg);
