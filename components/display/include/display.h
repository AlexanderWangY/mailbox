#pragma once

#define LCD_WIDTH 480
#define LCD_HEIGHT 320

#define LCD_TRANSFER_LINES 40

void display_init(void);
void display_sleep(void);
void display_wake(void);
void display_dim(void);
void display_normal(void);
