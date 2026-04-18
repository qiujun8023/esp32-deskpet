#pragma once
#include <stdint.h>

#include "ssd1306.h"

extern uint8_t draw_framebuf[SSD1306_BUFSIZE];

void draw_clear(void);

/* color: 1=亮,0=灭 */
void draw_pixel(int x, int y, uint8_t color);

void draw_fill_rect(int x, int y, int w, int h, uint8_t color);
void draw_fill_round_rect(int x, int y, int w, int h, int r, uint8_t color);
void draw_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color);
