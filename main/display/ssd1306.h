#pragma once
#include <stdint.h>

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_BUFSIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

void ssd1306_init(void);
void ssd1306_flush(const uint8_t* buf);
