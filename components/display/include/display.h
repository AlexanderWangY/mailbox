#pragma once

#include <stddef.h>
#include <stdint.h>
#define LCD_WIDTH 480
#define LCD_HEIGHT 320

#define LCD_TRANSFER_LINES 40

void display_init(void);
void display_sleep(void);
void display_wake(void);

void display_clear(void);

void display_text(const char *text);
const uint8_t *display_mail_png(size_t *size);
