#ifndef ULTRASONIC_H
#define ULTRASONIC_H
#include <stdint.h>
int ultrasonic_init(void);
/* 历史接口声明，当前F407使用board_sensors.h的range_read。
 * 水箱PA4触发、PA3回波；返回0成功，其他值无效。 */
int ultrasonic_read(uint32_t *pulse_us);
#endif
