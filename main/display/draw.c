#include "draw.h"

#include <stdlib.h>
#include <string.h>

uint8_t g_framebuf[SSD1306_BUFSIZE];

/* ---- 基础操作 ---- */

void draw_clear(void) {
    memset(g_framebuf, 0, SSD1306_BUFSIZE);
}

void draw_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    int byte_idx = (y / 8) * SSD1306_WIDTH + x;
    int bit_idx  = y % 8;
    if (color)
        g_framebuf[byte_idx] |= (1 << bit_idx);
    else
        g_framebuf[byte_idx] &= ~(1 << bit_idx);
}

/* ---- 矩形 ---- */

void draw_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++) draw_pixel(col, row, color);
}

/* ---- 圆角矩形 ---- */

void draw_fill_round_rect(int x, int y, int w, int h, int r, uint8_t color) {
    if (r < 1) {
        draw_fill_rect(x, y, w, h, color);
        return;
    }
    int max_r = (w < h ? w : h) / 2;
    if (r > max_r) r = max_r;

    /* 中间主体（三段矩形拼接） */
    draw_fill_rect(x + r, y, w - 2 * r, h, color);          // 中央竖条
    draw_fill_rect(x, y + r, r, h - 2 * r, color);          // 左侧
    draw_fill_rect(x + w - r, y + r, r, h - 2 * r, color);  // 右侧

    /* 四个圆角：逐像素判断是否在圆内 */
    for (int py = 0; py < r; py++) {
        for (int px = 0; px < r; px++) {
            int dx = r - 1 - px;
            int dy = r - 1 - py;
            if (dx * dx + dy * dy <= r * r) {
                draw_pixel(x + px, y + py, color);                  // 左上
                draw_pixel(x + w - 1 - px, y + py, color);          // 右上
                draw_pixel(x + px, y + h - 1 - py, color);          // 左下
                draw_pixel(x + w - 1 - px, y + h - 1 - py, color);  // 右下
            }
        }
    }
}

/* ---- 三角形（扫描线填充）---- */

static inline int min3(int a, int b, int c) {
    return a < b ? (a < c ? a : c) : (b < c ? b : c);
}
static inline int max3(int a, int b, int c) {
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

void draw_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) {
    int ymin = min3(y0, y1, y2);
    int ymax = max3(y0, y1, y2);

    for (int y = ymin; y <= ymax; y++) {
        int xs[2], cnt = 0;
        /* 对三条边求交点 */
        int ex[3] = {x0, x1, x2};
        int ey[3] = {y0, y1, y2};
        for (int i = 0; i < 3 && cnt < 2; i++) {
            int j  = (i + 1) % 3;
            int ay = ey[i], by = ey[j];
            int ax = ex[i], bx = ex[j];
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                xs[cnt++] = ax + (bx - ax) * (y - ay) / (by - ay);
            }
        }
        if (cnt == 2) {
            if (xs[0] > xs[1]) {
                int t = xs[0];
                xs[0] = xs[1];
                xs[1] = t;
            }
            for (int x = xs[0]; x <= xs[1]; x++) draw_pixel(x, y, color);
        }
    }
}
