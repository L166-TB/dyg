#ifndef TANK_SENSOR_H
#define TANK_SENSOR_H
#include <stdint.h>
/* 回波往返时间换算相对水位；缺失或超出水箱范围返回-1。 */
int tank_level_from_echo(uint32_t pulse_us, uint16_t full_mm,
                         uint16_t empty_mm, uint16_t margin_mm, uint16_t *distance_mm);
#endif
