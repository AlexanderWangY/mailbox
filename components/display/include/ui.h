#pragma once

typedef enum {
  UI_SCREEN_NO_MAIL,
  UI_SCREEN_HAS_MAIL,
  UI_SCREEN_READING,
  UI_SCREEN_DEBUG,
} ui_screen_t;

void ui_init(void);
