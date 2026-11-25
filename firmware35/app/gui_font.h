/* UTF-8解码后的中文字形及8像素ASCII字形。 */
#ifndef GUI_FONT_H
#define GUI_FONT_H
#include <stdint.h>
const uint8_t *gui_glyph(unsigned code);
#endif
