#ifndef BOARD_SENSORS_H
#define BOARD_SENSORS_H
#include "board_hal.h"
void range_init(TIM_TypeDef *timer,GPIO_TypeDef *echo,unsigned pin,unsigned af,unsigned channel);
int range_read(TIM_TypeDef *timer,GPIO_TypeDef *trig,unsigned pin,unsigned channel,uint32_t *pulse);
int board_dht(int16_t *temperature,uint16_t *humidity);
#endif
