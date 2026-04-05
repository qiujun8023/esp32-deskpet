#pragma once
#include <stdint.h>

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_BUFSIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)  // 1024 bytes

/* 初始化 I2C + SSD1306，显示屏清空 */
void ssd1306_init(void);

/* 将 1024 字节帧缓冲一次性推送到显示屏 */
void ssd1306_flush(const uint8_t *buf);
