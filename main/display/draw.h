#pragma once
#include <stdint.h>
#include "ssd1306.h"

/* 帧缓冲（外部可读，由 robo_eyes 填写后调用 ssd1306_flush） */
extern uint8_t g_framebuf[SSD1306_BUFSIZE];

/* 清空帧缓冲 */
void draw_clear(void);

/* 单像素（color: 1=亮, 0=灭）*/
void draw_pixel(int x, int y, uint8_t color);

/* 实心矩形 */
void draw_fill_rect(int x, int y, int w, int h, uint8_t color);

/* 实心圆角矩形（r = 圆角半径）*/
void draw_fill_round_rect(int x, int y, int w, int h, int r, uint8_t color);

/* 实心三角形（扫描线填充）*/
void draw_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color);
