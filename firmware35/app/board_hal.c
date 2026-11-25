/* STM32F407外设适配：TIM5只作微秒计时，SysTick完整交给LiteOS-M。 */
#include "board_hal.h"
#include "gui_font.h"
#include <stdio.h>
volatile uint16_t board_beep_mask;
/* ADC123_COMMON 的 CCR（0x40012300+4）：只用来配 ADCPRE 分频。 */
#define CANE_ADC_CCR (*(volatile uint32_t *)0x40012304u)
void TIM6_DAC_IRQHandler(void)
{
 if(TIM6->SR&TIM_SR_UIF){
  uint16_t mask=board_beep_mask&0x7000u;
  uint16_t high=(uint16_t)(~GPIOE->ODR)&mask;
  TIM6->SR=0;
  /* 原子写蜂鸣器引脚，避免中断覆盖任务正在更新的状态灯。 */
  GPIOE->BSRRL=high;
  GPIOE->BSRRH=(uint16_t)(0x7000u&~high);
 }
}
/* 无报警时停止音频定时器；不让空中断持续消耗仿真和CPU资源。 */
void board_set_beep(uint16_t mask)
{
 mask&=0x7000u;
 if(mask==board_beep_mask)return;
 board_beep_mask=mask;
 if(mask){TIM_Cmd(TIM6,ENABLE);}
 else{TIM_Cmd(TIM6,DISABLE);TIM6->SR=0;GPIOE->BSRRH=0x7000u;}
}
void board_gpio(GPIO_TypeDef *p,uint16_t pins,GPIOMode_TypeDef mode,GPIOPuPd_TypeDef pull)
{
 GPIO_InitTypeDef g;GPIO_StructInit(&g);g.GPIO_Pin=pins;g.GPIO_Mode=mode;
 g.GPIO_PuPd=pull;g.GPIO_OType=GPIO_OType_PP;g.GPIO_Speed=GPIO_Speed_50MHz;GPIO_Init(p,&g);
}
void board_af(GPIO_TypeDef *p,unsigned pin,unsigned af)
{board_gpio(p,1u<<pin,GPIO_Mode_AF,GPIO_PuPd_UP);GPIO_PinAFConfig(p,pin,af);}
uint32_t board_millis(void){return (uint32_t)LOS_TickCountGet();}
/* 统一16位微秒环计数；显式取模，避免仿真计数回绕被当作数十亿微秒。 */
void delay_us(uint32_t us)
{
 while(us){uint16_t span=us>30000u?30000u:(uint16_t)us,at=(uint16_t)TIM5->CNT;
  while((uint16_t)((uint16_t)TIM5->CNT-at)<span){}
  us-=span;
 }
}
void board_uart(USART_TypeDef *u,uint32_t baud)
{
 USART_InitTypeDef s;
 if(u==USART1){RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);board_af(GPIOA,9,GPIO_AF_USART1);board_af(GPIOA,10,GPIO_AF_USART1);}
 if(u==USART2){RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);board_af(GPIOD,5,GPIO_AF_USART2);board_af(GPIOD,6,GPIO_AF_USART2);}
 if(u==USART3){RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);board_af(GPIOB,10,GPIO_AF_USART3);board_af(GPIOB,11,GPIO_AF_USART3);}
 USART_StructInit(&s);s.USART_BaudRate=baud;USART_Init(u,&s);USART_Cmd(u,ENABLE);
}
void board_hal_init(void)
{
 RCC_ClocksTypeDef clk;TIM_TimeBaseInitTypeDef t;
 RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_GPIOB|RCC_AHB1Periph_GPIOC|RCC_AHB1Periph_GPIOD|RCC_AHB1Periph_GPIOE|RCC_AHB1Periph_GPIOF|RCC_AHB1Periph_GPIOG,ENABLE);
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5,ENABLE);RCC_GetClocksFreq(&clk);
 TIM_TimeBaseStructInit(&t);t.TIM_Prescaler=(clk.PCLK1_Frequency==clk.HCLK_Frequency?clk.PCLK1_Frequency:clk.PCLK1_Frequency*2u)/1000000u-1u;
 t.TIM_Period=65535u;TIM_TimeBaseInit(TIM5,&t);TIM_Cmd(TIM5,ENABLE);
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6,ENABLE);t.TIM_Period=249;TIM_TimeBaseInit(TIM6,&t);TIM_ClearFlag(TIM6,TIM_FLAG_Update);TIM_ITConfig(TIM6,TIM_IT_Update,ENABLE);NVIC_SetPriority(TIM6_DAC_IRQn,7);NVIC_EnableIRQ(TIM6_DAC_IRQn);TIM_Cmd(TIM6,DISABLE);
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1|RCC_APB2Periph_ADC2,ENABLE);
 /* ADC 全部走寄存器：工程里没有编译 StdPeriph 的 stm32f4xx_adc.c，调 ADC_xxx 会链接失败。
    ADCPRE=PCLK2/4=21MHz（F407 上限 36MHz），12 位、单次转换、序列长度 1。 */
 CANE_ADC_CCR=(CANE_ADC_CCR&~(3u<<16))|(1u<<16);
 {ADC_TypeDef *a;unsigned i;
  for(i=0;i<2u;i++){a=i?ADC2:ADC1;
   a->CR1=0u;a->SQR1=0u;a->SQR2=0u;
   a->CR2=1u;                                  /* ADON=1 上电 */
   {volatile uint32_t d;for(d=0;d<20000u;d++){}}  /* tSTAB 稳定时间 */
  }}
 board_gpio(GPIOA,GPIO_Pin_1|GPIO_Pin_2,GPIO_Mode_AN,GPIO_PuPd_NOPULL);
 board_gpio(GPIOC,GPIO_Pin_1,GPIO_Mode_AN,GPIO_PuPd_NOPULL);
 board_uart(USART1,115200);
}
int _write(int fd,char *p,int length)
{
 int i;(void)fd;
 for(i=0;i<length;i++){uint16_t start=(uint16_t)TIM5->CNT;while(!USART_GetFlagStatus(USART1,USART_FLAG_TXE))if((uint16_t)((uint16_t)TIM5->CNT-start)>2000u)return i;USART_SendData(USART1,(uint8_t)p[i]);}
 return length;
}
int board_adc(ADC_TypeDef *adc,unsigned channel,uint16_t *value)
{
 uint32_t at=board_millis(),sh;
 if(!adc||!value)return -1;
 /* 采样时间 144 周期（SMP=7）：通道 10~18 在 SMPR1，0~9 在 SMPR2。 */
 if(channel<10u){sh=channel*3u;adc->SMPR2=(adc->SMPR2&~(7u<<sh))|(7u<<sh);}
 else{sh=(channel-10u)*3u;adc->SMPR1=(adc->SMPR1&~(7u<<sh))|(7u<<sh);}
 adc->SQR1&=~0x0Fu;                 /* L=0：规则序列只有 1 个转换 */
 adc->SQR3=channel&0x1Fu;
 adc->SR=0u;                        /* 清 EOC/OVR */
 adc->CR2|=0x40000000u;             /* SWSTART 启动一次转换 */
 while(!(adc->SR&0x02u)){if(board_millis()-at>=5u)return -1;LOS_TaskDelay(1);}
 *value=(uint16_t)(adc->DR&0x0FFFu);/* 读 DR 同时清 EOC */
 return 0;
}
/* 辅助OLED仍用独立PD0/PD1，不复用显示数据总线。 */
static void oled_bit(uint16_t p,unsigned v){if(v)GPIO_SetBits(GPIOD,p);else GPIO_ResetBits(GPIOD,p);delay_us(3);}
static void oled_byte(uint8_t v){unsigned i;for(i=0;i<8;i++){oled_bit(GPIO_Pin_0,0);oled_bit(GPIO_Pin_1,v&128u);oled_bit(GPIO_Pin_0,1);v<<=1;}oled_bit(GPIO_Pin_0,0);oled_bit(GPIO_Pin_1,1);oled_bit(GPIO_Pin_0,1);oled_bit(GPIO_Pin_0,0);}
static void oled_begin(uint8_t control){oled_bit(GPIO_Pin_1,1);oled_bit(GPIO_Pin_0,1);oled_bit(GPIO_Pin_1,0);oled_byte(0x78);oled_byte(control);}
static void oled_end(void){oled_bit(GPIO_Pin_1,0);oled_bit(GPIO_Pin_0,1);oled_bit(GPIO_Pin_1,1);}
static void oled_command(uint8_t c){oled_begin(0);oled_byte(c);oled_end();}
void board_oled_init(void)
{
 unsigned i,p;static const uint8_t cfg[]={0xae,0x20,2,0xa1,0xc8,0x81,0x7f,0xa6,0xa8,0x3f,0xd3,0,0xd5,0x80,0xd9,0xf1,0xda,0x12,0xdb,0x40,0x8d,0x14,0xaf};
 board_gpio(GPIOD,GPIO_Pin_0|GPIO_Pin_1,GPIO_Mode_OUT,GPIO_PuPd_UP);GPIOD->OTYPER|=3u;
 for(i=0;i<sizeof(cfg);i++)oled_command(cfg[i]);
 for(p=0;p<8;p++){oled_command(0xb0+p);oled_command(0);oled_command(0x10);oled_begin(0x40);for(i=0;i<128;i++)oled_byte(0);oled_end();}
}
void board_oled_row(unsigned row,const char *s)
{
 unsigned p,i,x,y;for(p=0;p<2;p++){oled_command(0xb0+row*2+p);oled_command(0);oled_command(0x10);oled_begin(0x40);
 for(i=0;i<16;i++){const uint8_t *g=gui_glyph((unsigned char)s[i]);for(x=0;x<8;x++){uint8_t v=0;if(g)for(y=0;y<8;y++)if(g[p*8+y]&(128u>>x))v|=1u<<y;oled_byte(v);}}oled_end();}
}
