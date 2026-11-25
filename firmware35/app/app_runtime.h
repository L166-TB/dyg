#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include <stdint.h>
void app_start(void);
int app_command(uint8_t command);
uint32_t app_ack_serial(void);
uint32_t app_interlock_faults(void);
/* 过程任务只发布通风请求；PA6输出由环境控制任务统一写入。 */
void app_set_process_vent(unsigned enabled);
#endif
