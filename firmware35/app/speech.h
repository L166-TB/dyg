/* 所有入口由Display任务串行调用；串口中断只负责连续发送。 */
#ifndef SPEECH_H
#define SPEECH_H
#include <stdint.h>
void speech_init(void);
void speech_say(const char *utf8_text);
void speech_alert_text(const char *utf8_text);
void speech_poll(void);
uint8_t speech_busy(void);
#endif
