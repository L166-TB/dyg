/* 环境采集与执行器：所有数据来自实际外设，故障由控制策略统一处理。 */
#include "board_port.h"
#include "board_sensors.h"
#include "tank_sensor.h"
#include "tft35.h"
#include "cane_gui.h"
#include <string.h>
#include <stdio.h>
void board_safe_init(void)
{
 TIM_TimeBaseInitTypeDef t;TIM_OCInitTypeDef oc;RCC_ClocksTypeDef clk;uint32_t psc;
 /* PA7 只做普通推挽输出（不配成 TIM3_CH2 复用），原因见文件下方 pa7_apply 的说明。 */
 board_gpio(GPIOA,GPIO_Pin_7,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);board_gpio(GPIOB,GPIO_Pin_0,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);
 board_gpio(GPIOB,GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14,GPIO_Mode_IN,GPIO_PuPd_UP);board_all_off();
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);RCC_GetClocksFreq(&clk);
 /* PA6(TIM3_CH1)=BK_PWM_FAN_2 风扇调速，走 TIM3 硬件 PWM。
    PA7 不再用 TIM3_CH2 的 PWM，只在这一路配 PA6。 */
 psc=clk.PCLK1_Frequency*2u/1000000u-1u;              /* 1MHz 计数 */
 TIM_TimeBaseStructInit(&t);
 t.TIM_Prescaler=psc;t.TIM_Period=49u;                /* 1MHz/(49+1)=20kHz */
 TIM_TimeBaseInit(TIM3,&t);
 TIM_OCStructInit(&oc);oc.TIM_OCMode=TIM_OCMode_PWM1;oc.TIM_OutputState=TIM_OutputState_Enable;oc.TIM_OCPolarity=TIM_OCPolarity_High;
 oc.TIM_Pulse=0;TIM_OC1Init(TIM3,&oc);board_af(GPIOA,6,GPIO_AF_TIM3);TIM_Cmd(TIM3,ENABLE);
}

/* PA7 = 榨汁 / 搅拌直流电机（榨汁、提纯、煮糖三个工序共用）。
   电路上 dcmotor_1 是 lPin->PA7、rPin->GND 的两线元件，元件模型按 2kHz
   （元件属性 hz=2000）对引脚采样，只认"这一脚是不是被拉高"，不解析占空比。
   之前把 PA7 配成 TIM3_CH2 输出 20kHz PWM：20kHz 远高于模型 2kHz 的采样率，
   采样点基本落在低电平上，模型就判定电机没通电 —— 所以三个工序都不转。
   改成开关量输出：motor 非零即置高、为零即拉低，模型一定能看到稳定高电平。
   顺带断开了 PA7 与 PA6 共用 TIM3 时基的耦合，风扇那一路不受影响。 */
void pa7_apply(unsigned motor)
{
 static uint8_t prev=0xffu;
 uint8_t on=(motor!=0u)?1u:0u;
 if(prev==on)return;
 prev=on;
 if(on)GPIO_SetBits(GPIOA,GPIO_Pin_7);
 else   GPIO_ResetBits(GPIOA,GPIO_Pin_7);
}

void board_all_off(void){GPIO_ResetBits(GPIOA,GPIO_Pin_7);GPIO_ResetBits(GPIOB,GPIO_Pin_0);TIM3->CCR1=0;TIM3->CCR2=0;pa7_apply(0u);}
void board_sensor_init(void)
{
 /* PA1 = ADC1_IN1（MQ2 烟雾，经电阻分压），PA2 = ADC1_IN2（光敏电阻）。
    必须显式配成模拟输入：复位默认是浮空输入，ADC 采样浮空脚读到的值不可信，
    实测烟雾信号会一直是 0，导致"有烟也不报警"。 */
 board_gpio(GPIOA,GPIO_Pin_1|GPIO_Pin_2,GPIO_Mode_AIN,GPIO_PuPd_NOPULL);
 board_gpio(GPIOA,GPIO_Pin_4,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);range_init(TIM2,GPIOA,3,GPIO_AF_TIM2,TIM_Channel_4);}
void board_read_sample(AgSample *s,uint32_t now)
{
 static int16_t air_temperature;static uint16_t air_humidity;static uint32_t air_at;static uint8_t air_seen;
 static uint32_t dht_at;static uint8_t dht_seen;
 uint16_t raw;uint32_t pulse;int value;memset(s,0,sizeof(*s));s->at_ms=now;
 if(!board_adc(ADC1,ADC_Channel_1,&raw)){s->smoke_adc=raw;value=ag_normalize(raw,AG_SMOKE_CLEAR_ADC,AG_SMOKE_FULL_ADC);s->smoke=value;s->smoke_ppm=(uint16_t)ag_smoke_ppm(raw);s->valid|=AG_SMOKE;}
 if(!board_adc(ADC1,ADC_Channel_2,&raw)){s->light_adc=raw;value=ag_normalize(raw,AG_LIGHT_DARK_ADC,AG_LIGHT_BRIGHT_ADC);s->light=value;s->valid|=AG_LIGHT;}
 if(!range_read(TIM2,GPIOA,4,TIM_Channel_4,&pulse)){
  value=tank_level_from_echo(pulse,AG_HOPPER_FULL_MM,AG_HOPPER_EMPTY_MM,AG_HOPPER_MARGIN_MM,&s->distance_mm);
  if(value>=0){s->level=value;s->valid|=AG_LEVEL;}
 }
 /* 采样节拍已经提到 250ms，但 DHT11 单总线的两次读取最小间隔是 2s，
    读太快只会拿到坏帧（还要占 30ms 启动低电平）。所以温湿度单独按
    AG_DHT_PERIOD_MS 节流，其余时间沿用上一次成功值，仍受有效窗约束。
    有效窗从 5s 放宽到 AG_DHT_HOLD_MS（20s）：平台上一拖温度参数，单总线
    时序就被打断，连着几帧解不出来，5 秒一到就闪"未就绪"甚至冒报警话术。
    现在只要求"20 秒内有过一帧好数据"就算在线，拖动参数的几秒里屏幕继续
    显示最后一次真实读数，拖完 2 秒内就自动接上。 */
 if(!dht_seen||now-dht_at>=AG_DHT_PERIOD_MS){
  dht_seen=1;dht_at=now;
  if(!board_dht(&s->temperature10,&s->humidity10)){
   /* 参数被拖到量程外时 DHT 会给出 0℃/0% 这种明显离谱的帧：丢弃，
     继续沿用上一帧，避免屏幕上温度突然掉到 0 度又跳回来。 */
   if(s->temperature10>=0&&s->temperature10<=550&&s->humidity10<=1000){
    air_temperature=s->temperature10;air_humidity=s->humidity10;air_at=board_millis();air_seen=1;
   }else printf("DHT_RANGE_SKIP,t=%d,h=%u\n",(int)s->temperature10,(unsigned)s->humidity10);
  }else printf("DHT_RETRY,last_good_age=%lu\n",(unsigned long)(air_seen?board_millis()-air_at:0xffffffffu));
 }
 s->air_at_ms=air_at;
 if(air_seen&&board_millis()-air_at<=AG_DHT_HOLD_MS){s->temperature10=air_temperature;s->humidity10=air_humidity;s->valid|=AG_AIR;}
}
void board_apply(const AgControl *c)
{
 /* 风扇调速的唯一写点在 cane_outputs()（cane_hal.c），按 c->fan 的百分比
    直接写到 TIM3 通道1(PA6 -> BK_PWM_FAN_2)。
    这里绝不能再写一次：环境控制任务和过程控制任务都会走这条路径，
    两边用不同量纲（一个 0/1 布尔、一个 35/100 百分比）每 50ms 交替刷新同一个
    寄存器，PWM 就会来回扯，肉眼看就是风扇一卡一卡。
    本函数只负责与风扇无关的开关量。 */
 (void)c;
 /* 已移除水泵；PB0保持安全低电平，不作为输出设备。 */
 GPIO_ResetBits(GPIOB,GPIO_Pin_0);
}
uint8_t board_buttons(void)
{return (!GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_12)?1:0)|(!GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_13)?2:0)|(!GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)?4:0);}
void board_display_init(void){tft35_init();gui_init();}
void board_text_row(uint16_t y,const char *s,uint16_t color){(void)y;(void)s;(void)color;}
void board_display(const AgControl *c){(void)c;}
