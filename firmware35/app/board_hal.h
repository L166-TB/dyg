#ifndef BOARD_HAL_H
#define BOARD_HAL_H
#include "stm32f4xx.h"
#include "los_task.h"
#include "los_tick.h"
#include <stdint.h>
void board_hal_init(void);
void board_set_beep(uint16_t mask);
void board_gpio(GPIO_TypeDef *port,uint16_t pins,GPIOMode_TypeDef mode,GPIOPuPd_TypeDef pull);
void board_af(GPIO_TypeDef *port,unsigned pin,unsigned af);
void board_uart(USART_TypeDef *uart,uint32_t baud);
uint32_t board_millis(void);
void delay_us(uint32_t us);
int board_adc(ADC_TypeDef *adc,unsigned channel,uint16_t *value);
void board_oled_init(void);
void board_oled_row(unsigned row,const char *text);
#endif
