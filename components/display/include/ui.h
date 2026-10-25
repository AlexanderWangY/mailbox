#pragma once

#include <stdbool.h>

typedef enum {
  UI_SCREEN_NO_MAIL,
  UI_SCREEN_HAS_MAIL,
  UI_SCREEN_READING,
  UI_SCREEN_DEBUG,
} ui_screen_t;

void ui_init(void);
void ui_draw_empty_screen(void);
void ui_draw_mail_screen(void);
void ui_draw_reading_screen(void);
void ui_set_reading_message(const char *text);
void ui_set_battery_status(int percent, bool charging);
