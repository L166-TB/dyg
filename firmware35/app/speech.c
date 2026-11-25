/* SYN8086中文播报：按钮反馈有界排队，具体报警优先，BUSY由实际引脚决定。 */
#include "speech.h"
#include "cane.h"
#include "board_hal.h"
#include <stdio.h>
#include <string.h>
#define VOICE_TEXT 160u
#define VOICE_QUEUE 8u
#define BY_FORCE_MS 3000u
#define URGENT_FORCE_MS 800u
typedef struct {char text[VOICE_TEXT];uint32_t at;} VoiceItem;
static VoiceItem queue[VOICE_QUEUE];
static char urgent[VOICE_TEXT];
static uint8_t head,count;
static uint8_t frame[VOICE_TEXT+5];
static volatile uint16_t length,offset;
static volatile uint8_t reply;
static uint32_t sent_at,serial;
void USART3_IRQHandler(void)
{
 if(USART_GetITStatus(USART3,USART_IT_RXNE)!=RESET)reply=(uint8_t)USART_ReceiveData(USART3);
 if(USART_GetITStatus(USART3,USART_IT_TXE)!=RESET){
  if(offset<length)USART_SendData(USART3,frame[offset++]);
  else USART_ITConfig(USART3,USART_IT_TXE,DISABLE);
 }
}
void speech_init(void)
{
 board_uart(USART3,115200);board_gpio(GPIOC,GPIO_Pin_15,GPIO_Mode_IN,GPIO_PuPd_DOWN);
 length=offset=0;head=count=0;urgent[0]=0;
 NVIC_SetPriority(USART3_IRQn,6);NVIC_EnableIRQ(USART3_IRQn);USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);
 printf("SPEECH_INIT,UTF8,param=05,busy=PC15,queue=8\n");
}
/* BY(PC15)只作软信号：若它长期为高（模型未驱动/接线悬空），不能把播报永久卡死，
   连续高电平超过 force_ms 后强制放行，保证报警语音一定能发出去。
   force_ms 分两档：普通播报等满 3 秒（不打断正常句子），
   报警等 800ms 就抢话——报警晚 3 秒，烟已经起来了。 */
static uint8_t gate(uint32_t force_ms)
{
 static uint32_t by_since;static uint8_t forced;
 uint32_t now=cane_now();
 if(length!=offset){by_since=0;return 1;}
 if(GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_15)!=RESET){
  if(!by_since)by_since=now?now:1u;
  if(now-by_since<force_ms)return 1;
  if(!forced){forced=1;printf("SPEECH_BY_TIMEOUT,force_send\n");}
  return 0;
 }
 by_since=0;forced=0;return 0;
}
uint8_t speech_busy(void){return gate(BY_FORCE_MS);}
static int allowed(const char *text)
{return text&&text[0]&&strlen(text)<VOICE_TEXT;}
/* 同一条文案短时间重复出现时只播一次：状态机与界面任务可能在同一事件上都发过一次，
   这里做最后一道去重，避免同一句报警连播两遍。 */
static int duplicate(const char *text)
{
 static char last[VOICE_TEXT];static uint32_t last_at;
 uint32_t now=cane_now();
 if(last[0]&&strcmp(last,text)==0&&now-last_at<4000u){printf("SPEECH_DUP,skip\n");return 1;}
 strncpy(last,text,sizeof(last)-1u);last[sizeof(last)-1u]=0;last_at=now;
 return 0;
}
void speech_say(const char *text)
{
 unsigned at;
 if(!allowed(text)){printf("SPEECH_REJECT,length\n");return;}
 if(duplicate(text))return;
 if(count>=VOICE_QUEUE){head=(uint8_t)((head+1u)%VOICE_QUEUE);count--;printf("SPEECH_QUEUE,drop_oldest\n");}
 at=(head+count)%VOICE_QUEUE;strcpy(queue[at].text,text);queue[at].at=cane_now();count++;
 printf("SPEECH_QUEUE,count=%u,text=%s\n",count,text);
}
void speech_alert_text(const char *text)
{
 if(!allowed(text))return;
 if(duplicate(text))return;
 /* 报警替换尚未播放的普通导航，当前帧不截断，不发送不兼容的停止帧。 */
 strcpy(urgent,text);head=count=0;
 printf("SPEECH_ALERT,%s\n",text);
}
static void send(const char *text)
{
 unsigned n=strlen(text);USART_ITConfig(USART3,USART_IT_TXE,DISABLE);
 frame[0]=0xfd;frame[1]=(uint8_t)((n+2u)>>8);frame[2]=(uint8_t)(n+2u);frame[3]=1;frame[4]=5;
 memcpy(frame+5,text,n);offset=0;length=(uint16_t)(n+5u);sent_at=cane_now();serial++;
 USART_ITConfig(USART3,USART_IT_TXE,ENABLE);printf("SPEECH_UTF8,%s\n",text);
}
void speech_poll(void)
{
 static uint32_t reported;
 if(reply){printf("SPEECH_REPLY,%02x\n",reply);reply=0;}
 if(offset==length&&length&&reported!=serial){printf("SPEECH_TX,bytes=%u,busy=%u\n",length,speech_busy());reported=serial;}
 if(offset<length&&cane_now()-sent_at>5000u){USART_ITConfig(USART3,USART_IT_TXE,DISABLE);length=offset=0;printf("SPEECH_TIMEOUT\n");}
 if(gate(urgent[0]?URGENT_FORCE_MS:BY_FORCE_MS))return;
 if(urgent[0]){send(urgent);urgent[0]=0;return;}
 while(count){VoiceItem *item=&queue[head];head=(uint8_t)((head+1u)%VOICE_QUEUE);count--;
  if(cane_now()-item->at<=30000u){send(item->text);return;}
  printf("SPEECH_QUEUE,expired\n");
 }
}
