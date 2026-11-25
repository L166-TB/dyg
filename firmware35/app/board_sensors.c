/* 脉宽由硬件输入捕获，任务等待时让出CPU；单总线微秒采样有严格超时。 */
#include "board_sensors.h"
#include "agri_control.h"
#include "los_interrupt.h"
#include <stdio.h>
void range_init(TIM_TypeDef *tim,GPIO_TypeDef *p,unsigned pin,unsigned af,unsigned channel)
{
 RCC_ClocksTypeDef c;TIM_TimeBaseInitTypeDef t;TIM_ICInitTypeDef in;uint32_t hz;
 if(tim==TIM1)RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1,ENABLE);else RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
 RCC_GetClocksFreq(&c);hz=tim==TIM1?c.PCLK2_Frequency:c.PCLK1_Frequency;if(hz!=c.HCLK_Frequency)hz*=2;
 board_af(p,pin,af);TIM_TimeBaseStructInit(&t);t.TIM_Prescaler=hz/1000000u-1u;t.TIM_Period=65535;TIM_TimeBaseInit(tim,&t);
 TIM_ICStructInit(&in);in.TIM_Channel=channel;in.TIM_ICPolarity=TIM_ICPolarity_Rising;TIM_ICInit(tim,&in);
 in.TIM_Channel=channel==TIM_Channel_1?TIM_Channel_2:TIM_Channel_3;in.TIM_ICSelection=TIM_ICSelection_IndirectTI;in.TIM_ICPolarity=TIM_ICPolarity_Falling;TIM_ICInit(tim,&in);
}
int range_read(TIM_TypeDef *tim,GPIO_TypeDef *trig,unsigned pin,unsigned channel,uint32_t *pulse)
{
 uint32_t at,rising,falling;uint16_t flag=channel==TIM_Channel_1?TIM_FLAG_CC2:TIM_FLAG_CC3;
 TIM_SetCounter(tim,0);TIM_ClearFlag(tim,TIM_FLAG_CC1|TIM_FLAG_CC2|TIM_FLAG_CC3|TIM_FLAG_CC4);TIM_Cmd(tim,ENABLE);
 GPIO_SetBits(trig,1u<<pin);delay_us(12);GPIO_ResetBits(trig,1u<<pin);at=board_millis();
 while(!TIM_GetFlagStatus(tim,flag)){if(board_millis()-at>=30u){TIM_Cmd(tim,DISABLE);return -1;}LOS_TaskDelay(1);}
 rising=channel==TIM_Channel_1?TIM_GetCapture1(tim):TIM_GetCapture4(tim);falling=channel==TIM_Channel_1?TIM_GetCapture2(tim):TIM_GetCapture3(tim);TIM_Cmd(tim,DISABLE);
 *pulse=(uint16_t)(falling-rising);return *pulse>=100u&&*pulse<=25000u?0:-1;
}
static int wait_level(unsigned high)
{uint16_t at=(uint16_t)TIM5->CNT;while(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)!=high)if((uint16_t)((uint16_t)TIM5->CNT-at)>120u)return -1;return 0;}
int board_dht(int16_t *temperature,uint16_t *humidity)
{
 uint8_t bytes[5]={0};unsigned i=0,j=0;int fail=0,decoded;UINT32 irq;uint32_t high_at,width=0,min_high=1000u,max_high=0;
 /* OS延时按tick取整，留足启动低脉冲余量，不能贴着器件最小门限。 */
 board_gpio(GPIOA,GPIO_Pin_0,GPIO_Mode_OUT,GPIO_PuPd_UP);GPIO_ResetBits(GPIOA,GPIO_Pin_0);LOS_TaskDelay(30);
 /* 从释放总线到最后一位统一保护，避免释放后被调度而漏掉应答。 */
 irq=LOS_IntLock();GPIO_SetBits(GPIOA,GPIO_Pin_0);delay_us(20);board_gpio(GPIOA,GPIO_Pin_0,GPIO_Mode_IN,GPIO_PuPd_UP);
 if(wait_level(0))fail=1;else if(wait_level(1))fail=2;else if(wait_level(0))fail=3;
 /* 测量每位高脉宽，不在固定40us处采样，降低边沿与仿真量化误差。 */
 for(i=0;i<5&&!fail;i++)for(j=0;j<8;j++){
  if(wait_level(1)){fail=4;break;}high_at=TIM5->CNT;
  if(wait_level(0)){fail=5;break;}width=(uint16_t)((uint16_t)TIM5->CNT-(uint16_t)high_at);
  if(width<min_high)min_high=width;
  if(width>max_high)max_high=width;
  bytes[i]=(bytes[i]<<1)|(width>=50u?1u:0u);
 }
 LOS_IntRestore(irq);decoded=fail?-1:ag_decode_dht(bytes,temperature,humidity);
 if(decoded)printf("DHT_FRAME,phase=%u,byte=%u,bit=%u,width=%lu/%lu,data=%u/%u/%u/%u/%u\n",fail,i,j,(unsigned long)min_high,(unsigned long)max_high,bytes[0],bytes[1],bytes[2],bytes[3],bytes[4]);
 return decoded;
}
