/* 3.5寸 16位8080并口（320x480 竖屏），PF0~PF15 数据，PG0~4 控制，PG5~9 触摸。
 * 平台 delay_us 比真实时间慢约三个数量级，总线只要求电平先后，这里用空循环。 */
#include "tft35.h"
#include "board_hal.h"
#include <stdio.h>
#define CS GPIO_Pin_0
#define RS GPIO_Pin_1
#define WR GPIO_Pin_2
#define RD GPIO_Pin_3
#define RST GPIO_Pin_4
static void high(uint16_t pins){GPIOG->ODR|=pins;}
static void low(uint16_t pins){GPIOG->ODR&=~pins;}
static void dly(void){volatile uint32_t n=3;while(n--){ }(void)board_millis();}
static void bus(uint16_t v)
{
 GPIOF->ODR=v;
 dly();
 low(WR);
 dly();
 high(WR);
 dly();
}
static void command(uint8_t c){low(CS);low(RS);bus(c);high(RS);}
static void data(uint8_t d){bus(d);}
static void data16(uint16_t d){bus((uint8_t)(d>>8));bus((uint8_t)d);}
static void reg(uint8_t c,const uint8_t *p,unsigned n){command(c);while(n--)data(*p++);high(CS);}
static uint16_t read_word(void){uint16_t v;low(RD);dly();v=GPIOF->IDR;high(RD);dly();return v;}
void tft35_init(void)
{
 unsigned i;uint16_t id[4];
 /* ILI9488 320x480 初始化；平台 3.5 寸模块为 16 位并口。 */
 static const uint8_t cfg[]={
  0xE0,15,0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1E,0x0F,
  0xE1,15,0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F,
  0xC0,2,0x17,0x15,
  0xC1,1,0x41,
  0xC5,3,0x00,0x12,0x80,
  0x36,1,0x48,
  0x3A,1,0x55,
  0xB0,1,0x00,
  0xB1,1,0xA0,
  0xB4,1,0x02,
  0xB6,3,0x02,0x02,0x3B,
  0xE9,1,0x00,
  0xF7,4,0xA9,0x51,0x2C,0x82
 };
 board_gpio(GPIOF,0xffffu,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);board_gpio(GPIOG,0x00ffu,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);
 board_gpio(GPIOG,GPIO_Pin_8,GPIO_Mode_IN,GPIO_PuPd_NOPULL);board_gpio(GPIOG,GPIO_Pin_9,GPIO_Mode_IN,GPIO_PuPd_UP);
 high(CS|RS|WR|RD|GPIO_Pin_5);low(RST);LOS_TaskDelay(20);high(RST);LOS_TaskDelay(120);
 command(1);high(CS);LOS_TaskDelay(120);
 command(0xd3);low(CS);board_gpio(GPIOF,0xffffu,GPIO_Mode_IN,GPIO_PuPd_NOPULL);for(i=0;i<4;i++)id[i]=read_word();high(CS);
 board_gpio(GPIOF,0xffffu,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);printf("TFT35_ID,%04x/%04x/%04x/%04x\n",id[0],id[1],id[2],id[3]);
 for(i=0;i<sizeof(cfg);){uint8_t c=cfg[i++],n=cfg[i++];reg(c,cfg+i,n);i+=n;}
 command(0x11);high(CS);LOS_TaskDelay(120);command(0x29);high(CS);LOS_TaskDelay(20);
 command(0x2a);data(0);data(0);data(0x01);data(0x3f);
 command(0x2b);data(0);data(0);data(0x01);data(0xdf);
 command(0x2c);
 for(i=0;i<153600u;i++){bus(0x0843u);if((i%320u)==319u)LOS_TaskDelay(1);}
 high(CS);
}
void tft35_span(uint16_t x,uint16_t y,const uint16_t *p,uint16_t n)
{
 uint16_t end;if(x>=320u||y>=480u||!n)return;if(n>320u-x)n=320u-x;end=x+n-1u;
 command(0x2a);data16(x);data16(end);command(0x2b);data16(y);data16(y);command(0x2c);
 while(n--)bus(*p++);
 high(CS);
}
static uint16_t touch_adc(uint8_t cmd)
{
 unsigned i;uint16_t v=0;
 low(GPIO_Pin_5|GPIO_Pin_6);dly();
 for(i=0;i<8;i++){if(cmd&128u)high(GPIO_Pin_7);else low(GPIO_Pin_7);high(GPIO_Pin_6);dly();low(GPIO_Pin_6);dly();cmd<<=1;}
 dly();for(i=0;i<16;i++){high(GPIO_Pin_6);dly();v=(v<<1)|GPIO_ReadInputDataBit(GPIOG,GPIO_Pin_8);low(GPIO_Pin_6);dly();}
 high(GPIO_Pin_5);dly();return (v>>3)&4095u;
}
int tft35_touch(uint16_t *x,uint16_t *y)
{
 uint16_t ax,ay;if(GPIO_ReadInputDataBit(GPIOG,GPIO_Pin_9))return 0;
 ax=touch_adc(0xd0);ay=touch_adc(0x90);
 *x=(uint32_t)ax*319u/4095u;*y=(uint32_t)ay*479u/4095u;return 1;
}
