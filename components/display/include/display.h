#pragma once

#include <stdint.h>
#define LCD_WIDTH 480
#define LCD_HEIGHT 320

#define LCD_TRANSFER_LINES 40

typedef uint16_t display_color_t;

typedef struct {
  uint16_t height;
  uint16_t width;
  const uint16_t *pixels;
} display_image_t;

display_color_t rbg656(uint8_t r, uint8_t g, uint8_t b);

void display_init(void);
void display_sleep(void);
void display_wake(void);

void display_clear(display_color_t color);

void display_fill_rect(
  int x, int y, int w, int h, display_color_t color
);

void display_draw_image(int x, int y, const display_image_t *image);

void display_draw_char(
  int x, int y,
  const char c,
  display_color_t color
);

void display_draw_text(
  int x, int y,
  const char *text,
  display_color_t color
);

// Text centered in display
void display_draw_text_centered(
  const char *text,
  display_color_t color
);

