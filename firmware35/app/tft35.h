/* 3.5寸 ILI9488/ILI9341 兼容 16位并口（320x480 竖屏）+ XPT2046 触摸。 */
#ifndef TFT35_H
#define TFT35_H
#include <stdint.h>
#define TFT35_WIDTH 320u
#define TFT35_HEIGHT 480u
void tft35_init(void);
void tft35_span(uint16_t x,uint16_t y,const uint16_t *pixels,uint16_t length);
int tft35_touch(uint16_t *x,uint16_t *y);
#endif
