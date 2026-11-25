/* 过程传感器和执行器移植；超时让出CPU，微秒总线读取只作短时临界区。 */
#include "cane.h"
#include "board_sensors.h"
#include "board_port.h"
#include "los_interrupt.h"
#include <stdio.h>
#include <string.h>
static volatile uint8_t tare_requested=1;static volatile uint32_t tare_serial;static int32_t tare;static int ir_stage;
/* 去皮请求分“需要播报”和“静默”两种；1=这次去皮完成后要出声。 */
volatile uint8_t cane_tare_announce;

void cane_request_tare_loud(unsigned say)
{cane_tare_announce=(uint8_t)(say?1u:0u);tare_requested=1;}
uint32_t cane_now(void){return board_millis();}
static void bit(GPIO_TypeDef *p,uint16_t n,unsigned on){if(on)GPIO_SetBits(p,n);else GPIO_ResetBits(p,n);}
static int weight_read(int32_t *out)
{
 uint32_t start=cane_now(),raw=0;unsigned i;UINT32 irq;GPIO_ResetBits(GPIOC,GPIO_Pin_2);
 while(GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_3)){if(cane_now()-start>=120u)return -1;LOS_TaskDelay(1);}
 irq=LOS_IntLock();for(i=0;i<24;i++){GPIO_SetBits(GPIOC,GPIO_Pin_2);delay_us(1);raw=(raw<<1)|GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_3);GPIO_ResetBits(GPIOC,GPIO_Pin_2);delay_us(1);}
 GPIO_SetBits(GPIOC,GPIO_Pin_2);delay_us(1);GPIO_ResetBits(GPIOC,GPIO_Pin_2);LOS_IntRestore(irq);
 *out=(int32_t)((raw&0x800000u)?raw|0xff000000u:raw);return 0;
}
static int scl(unsigned high)
{
 unsigned i;bit(GPIOB,GPIO_Pin_8,high);if(!high)return 0;
 for(i=0;i<100;i++){if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8)!=RESET){delay_us(4);return 0;}delay_us(1);}return -1;
}
static void sda(unsigned high){bit(GPIOB,GPIO_Pin_9,high);delay_us(4);}
static int start(void){sda(1);if(scl(1))return -1;sda(0);scl(0);return 0;}
static void stop(void){scl(0);sda(0);scl(1);sda(1);}
static int send_byte(uint8_t value)
{
 unsigned i;int ack;for(i=0;i<8;i++){sda(value&0x80);if(scl(1))return -1;scl(0);value<<=1;}
 sda(1);if(scl(1))return -1;ack=GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_9)!=RESET;scl(0);return ack?-1:0;
}
static int receive_byte(uint8_t *value,unsigned nack)
{
 unsigned i;*value=0;sda(1);for(i=0;i<8;i++){if(scl(1))return -1;*value=(uint8_t)((*value<<1)|(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_9)!=RESET));scl(0);}
 sda(nack);if(scl(1))return -1;scl(0);sda(1);return 0;
}
static uint8_t crc8(const uint8_t *bytes,unsigned length)
{uint8_t c=0;unsigned i,j;for(i=0;i<length;i++){c^=bytes[i];for(j=0;j<8;j++)c=(c&0x80)?(uint8_t)((c<<1)^7):(uint8_t)(c<<1);}return c;}
static int infrared_at(int16_t *temp,uint8_t address)
{
 uint8_t f[5]={address,7,(uint8_t)(address+1u),0,0},pec;uint16_t raw;int rc=-1;
 ir_stage=1;if(start())goto end;
 ir_stage=2;if(send_byte(f[0]))goto end;
 ir_stage=3;if(send_byte(f[1]))goto end;
 ir_stage=4;if(start())goto end;
 ir_stage=5;if(send_byte(f[2]))goto end;
 ir_stage=6;if(receive_byte(&f[3],0)||receive_byte(&f[4],0)||receive_byte(&pec,1))goto end;
 raw=((uint16_t)f[4]<<8)|f[3];
 {static uint32_t logged;if(cane_now()-logged>=5000u){logged=cane_now();printf("CANE_IR,addr=%u,raw=%u,pec=%u,expected=%u\n",address,raw,pec,crc8(f,5));}}
 ir_stage=7;if(!(raw&0x8000u)&&crc8(f,5)==pec){int t=((int)raw*2-27315+5)/10;if(t>=-700&&t<=3800){*temp=(int16_t)t;rc=0;ir_stage=0;}}
end:stop();return rc;
}
static int infrared_read(int16_t *temp)
{
 if(!infrared_at(temp,0xb4))return 0;
 /* 官方BK模型保留地址0作为兼容回退，所有数据仍经SMBus读取。 */
 return infrared_at(temp,0);
}
/* 红外测温确认（防"1 度闪现"）。
   MLX90614 上电首读、总线抖动或后台数据源刚切换的那一帧，寄存器可能还没刷新，
   读回来算出的可能是个接近冰点的怪值。实测进煮糖页会先显示 1.0 度，
   过一拍才跳到 105.0 度，就是这一帧。
   规则：
     1) 上电后第一帧一律压住，不当作温度；
     2) 之后与已确认值相差不超过 IR_TRACK_10（5.0 度）就正常跟踪，升降温不受影响；
     3) 大跳变先压一帧，下一帧仍接近才认，单帧毛刺上不了屏；
     4) 大跳变期间若已有旧值，继续显示旧值，屏幕不会闪成 0 度或"未就绪"。
   返回 0 表示 *out 已写入可信温度，返回 1 表示这一帧没有任何可信值。 */
#define IR_TRACK_10 50
static int16_t ir_out,ir_pend;   /* 已确认值 / 待确认候选值 */
static uint8_t ir_warm,ir_ready;
static int cane_ir_confirm(int16_t t,int16_t *out)
{
 int d;
 if(!ir_warm){ir_warm=1;ir_pend=t;ir_out=t;return 1;}   /* 首帧不可信，压住 */
 if(ir_ready){
  d=t-ir_out;
  if(d>=-IR_TRACK_10&&d<=IR_TRACK_10){ir_out=t;*out=t;return 0;}   /* 正常跟踪 */
 }
 d=t-ir_pend;
 if(d>=-IR_TRACK_10&&d<=IR_TRACK_10){ir_out=t;ir_pend=t;ir_ready=1;*out=t;return 0;}  /* 连续两帧接近，确认 */
 ir_pend=t;                                             /* 跳变：先记下，下一帧再看 */
 *out=ir_out;
 return ir_ready?0:1;                                   /* 有旧值就先用旧值顶上 */
}

void cane_hardware_init(void)
{
 TIM_TimeBaseInitTypeDef t;TIM_OCInitTypeDef oc;RCC_ClocksTypeDef clk;unsigned i;
 /* ---- PA7 榨汁/搅拌电机 + PA6 风扇：必须在这里配 ----
    这两脚原来只在 board_safe_init() 里配置，而 board_safe_init() 没有任何调用点
    （只有声明和定义），结果 PA7 一直停在复位默认的浮空输入状态：
    推挽输出没配上，pa7_apply() 里那句 GPIO_SetBits 写下去等于写进输入寄存器，
    引脚根本不上电，dcmotor_1 全流程都不转。这里补齐（重复配置无害）。 */
 board_gpio(GPIOA,GPIO_Pin_7,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);
 GPIO_ResetBits(GPIOA,GPIO_Pin_7);
 /* PA6 = TIM3_CH1 = BK_PWM_FAN_2 风扇调速：TIM3 时基 1MHz / ARR=49 -> 20kHz。
    风扇与人无关（元件 hz=2000 是采样率，CCR 越大占空比越高，肉眼能看出转速差）。 */
 {uint32_t psc=clk.PCLK1_Frequency*2u/1000000u-1u;
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);TIM_TimeBaseStructInit(&t);
  t.TIM_Prescaler=psc;t.TIM_Period=49u;TIM_TimeBaseInit(TIM3,&t);}
 TIM_OCStructInit(&oc);oc.TIM_OCMode=TIM_OCMode_PWM1;oc.TIM_OutputState=TIM_OutputState_Enable;
 oc.TIM_OCPolarity=TIM_OCPolarity_High;oc.TIM_Pulse=0;
 TIM_OC1Init(TIM3,&oc);board_af(GPIOA,6,GPIO_AF_TIM3);TIM_Cmd(TIM3,ENABLE);
 /* 按键只剩 PC10~PC13（PC14 的称重键已从电路删除，别再上拉扫描空脚）；
    新增的 PB14 是安全页/确认报警键。 */
 board_gpio(GPIOC,GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_7|0x3c00u,GPIO_Mode_IN,GPIO_PuPd_UP);
 board_gpio(GPIOB,GPIO_Pin_14,GPIO_Mode_IN,GPIO_PuPd_UP);
 board_gpio(GPIOC,GPIO_Pin_2|GPIO_Pin_6,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);board_gpio(GPIOC,GPIO_Pin_3,GPIO_Mode_IN,GPIO_PuPd_NOPULL);
 /* PC1 = ADC2_IN11（线性霍尔），同样必须配成模拟输入，浮空脚读出来的转速是假的。 */
 board_gpio(GPIOC,GPIO_Pin_1,GPIO_Mode_AIN,GPIO_PuPd_NOPULL);
 board_gpio(GPIOB,GPIO_Pin_8|GPIO_Pin_9,GPIO_Mode_OUT,GPIO_PuPd_UP);GPIOB->OTYPER|=GPIO_Pin_8|GPIO_Pin_9;GPIO_SetBits(GPIOB,GPIO_Pin_8|GPIO_Pin_9);
 board_gpio(GPIOE,0x7fffu,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);GPIO_ResetBits(GPIOE,0x7fffu);
 board_gpio(GPIOD,GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_7,GPIO_Mode_OUT,GPIO_PuPd_NOPULL);GPIO_ResetBits(GPIOD,GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_7);
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4,ENABLE);RCC_GetClocksFreq(&clk);TIM_TimeBaseStructInit(&t);t.TIM_Prescaler=clk.PCLK1_Frequency*2/1000000u-1;t.TIM_Period=19999;TIM_TimeBaseInit(TIM4,&t);
 TIM_OCStructInit(&oc);oc.TIM_OCMode=TIM_OCMode_PWM1;oc.TIM_OutputState=TIM_OutputState_Enable;oc.TIM_OCPolarity=TIM_OCPolarity_High;oc.TIM_Pulse=1500;
 for(i=12;i<16;i++)board_af(GPIOD,i,GPIO_AF_TIM4);
 TIM_OC1Init(TIM4,&oc);TIM_OC2Init(TIM4,&oc);TIM_OC3Init(TIM4,&oc);oc.TIM_Pulse=0;TIM_OC4Init(TIM4,&oc);TIM_Cmd(TIM4,ENABLE);
 range_init(TIM1,GPIOA,8,GPIO_AF_TIM1,TIM_Channel_1);
}
void cane_read(CaneSample *s)
{
 int32_t raw;uint32_t pulse;memset(s,0,sizeof(*s));s->fire=!GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_4);s->person=!GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_5);
 /* 称重和分装读的是同一个 HX711，读数来源完全一致，区别只在界面单位。
    平台 HX711 器件把控制盘上的 1 个单位放大成了 420 个计数：
    控制盘显示 100 时，读回来的是 42000。所以这里必须除以 420 还原成面板值。
    还原后内部统一以“0.1 单位”存 weight10（= 面板值 * 10）：
     称重页把面板值当 kg 读（面板 100 -> 100.0 kg）
     分装页把面板值当 g  读（面板 100 -> 100 g）

    这个倍数由两次实测反推得到，实测记录：
      面板 100 -> raw 42000  -> /420 = 100 -> 屏 "100.0"
      面板 500 -> raw 210000 -> /420 = 500 -> 屏 "500.0"
    （早前除以 42 会得到 1000，说明倍数少算了一个 10。） */
 #define HX711_SCALE 420
 if(!weight_read(&raw)){
  if(tare_requested){tare=raw;tare_requested=0;tare_serial++;}
  s->tare_serial=tare_serial;
  s->raw_weight=(uint32_t)(raw<0?0:raw);
  raw-=tare;
  raw=raw<=0?0:raw/HX711_SCALE;   /* 还原成控制盘面板值 */
  raw=raw*10;                      /* 换成 0.1 单位 */
  s->weight10=(uint32_t)raw;
  s->valid|=CANE_WEIGHT;
 }
 {int16_t ir=0;if(!infrared_read(&ir)&&!cane_ir_confirm(ir,&s->object10))s->valid|=CANE_IR;}
 if(!range_read(TIM1,GPIOC,6,TIM_Channel_1,&pulse)){s->distance_mm=pulse*343u/2000u;if(s->distance_mm>=20&&s->distance_mm<=4000)s->valid|=CANE_DISTANCE;}
 if(!board_adc(ADC2,ADC_Channel_11,&s->hall_adc)){s->speed10=((int32_t)s->hall_adc*3300/4095-2500)*10/15;s->valid|=CANE_HALL;}
 s->at=cane_now();
}
void cane_request_tare(void){tare_requested=1;}
uint32_t cane_tare_serial(void){return tare_serial;}void cane_outputs(const CaneState *c,uint32_t now)
{
 uint16_t leds=0,beep=0;
 static uint16_t prev_servo[4]={0xffffu,0xffffu,0xffffu,0xffffu},prev_fan=0xffffu,prev_leds=0xffffu,prev_pwm=0xffffu;
 /* 只在目标变化时更新寄存器；PWM由定时器持续生成。
    servo[0]=分装阀 PD13，servo[1]=加料门 PD12。
    servo[2]（液体出料闸）和 servo[3]（提纯过滤阀）**都写在 PD14/TIM4_CH3 这一路**，
    由这里做仲裁：过滤阀给了非零角度就按过滤阀走，否则退回液体出料闸，
    保证同一时刻只有一个角度占着这条 PWM 线，两个动作不会互相拉扯。 */
 if(prev_servo[0]!=c->servo[0]){TIM4->CCR2=500u+(uint32_t)c->servo[0]*2000u/180u;prev_servo[0]=c->servo[0];}
 if(prev_servo[1]!=c->servo[1]){TIM4->CCR1=500u+(uint32_t)c->servo[1]*2000u/180u;prev_servo[1]=c->servo[1];}
 {uint16_t a3=c->servo[3]?(c->servo[3]>180u?180u:c->servo[3]):c->servo[2];
  if(prev_servo[2]!=a3){TIM4->CCR3=500u+(uint32_t)a3*2000u/180u;prev_servo[2]=a3;}}
 /* PA6 = TIM3_CH1 = BK_PWM_FAN_2，这是整个工程里唯一给风扇写 PWM 的地方。
    其它任何任务（board_apply / app_runtime）都不许再碰这个寄存器，
    否则两边用不同量纲每几十毫秒交替刷新，风扇就会一卡一卡。
    只在占空比目标变化时写一次寄存器，PWM 由定时器硬件持续输出。
    TIM3 时基 1MHz / ARR=49 => 20kHz，所以 CCR = 百分比% * ARR / 100，
    满量程只有 50 个计数（2% 一档），比原来 1000 个计数粗，但风扇调速够用。 */
 {uint32_t pwm=(uint32_t)c->fan*50u/100u;
  if(prev_pwm!=pwm){TIM3->CCR1=pwm;prev_pwm=(uint16_t)pwm;}}
 /* TIM4_CH4 接的是 PD15。这一路以前直接跟 c->fan 走，
     于是 PA6(TIM3_CH1) 和 PD15(TIM4_CH4) 会一起动 —— 看起来就是"两个风扇一起转"。
     现在 PD15 只作为 BK_PWM_FAN_2 的降温开关量：只有处于降温档才给满占空比，
     通风档和停机都保持 0，保证同一次只有 PA6 那一路在转。 */
 {uint16_t f2=c->fan2?20000u:0u;
  if(prev_fan!=f2){TIM4->CCR4=f2;prev_fan=f2;}}
 /* PA7 = 蔗汁/糖浆搅拌直流电机（榨汁、提纯、煮糖三个工序共用）。
    **不再写 TIM3->CCR2**：平台 dcmotor 元件（hz=2000）按 2kHz 采样引脚电平，
    对 20kHz PWM 采样不到占空比，判定为没通电，所以电机一直不转。
    改成开关量：motor 非零就把 PA7 拉高、为零拉低，由 pa7_apply 统一处理
    （内部做了变化检测，不会每拍重复写寄存器）。 */
 pa7_apply((unsigned)c->motor);
 /* 电路上只剩一只 RGB 灯 rgb_led_3：R=PE6、G=PE7、B=PE8。
    它只做系统状态灯——正常运行绿灯，任何报警或异常红灯，不承担别的显示职责；
    原来 PE0~PE11 上那几只按称重/温度/液位分别点亮的灯已经从电路上删掉，
    这里也不再保留它们的功能，避免把旧灯的画法挪到这只灯上。 */
 {unsigned abnormal=(c->alarm||c->faults||c->environment_faults||c->sample.fire||c->sample.person)?1u:0u;
  leds=(uint16_t)(abnormal?(1u<<6):(1u<<7));
 }
 /* 明火、人员、流程报警各占一路，节拍不同便于现场分辨。 */
 if(c->sample.fire&&(now/200u)%2u)beep|=GPIO_Pin_12;
 if(c->sample.person&&(now/300u)%2u)beep|=GPIO_Pin_13;
 if(c->alarm&&(now/500u)%2u)beep|=GPIO_Pin_14;
 /* 只复位这 3 个灯脚：PE0~PE5 被屏占用，PE9~PE15 还有别的用途，不能一起拉掉。 */
 if(prev_leds!=leds){GPIOE->BSRRL=leds;GPIOE->BSRRH=(uint16_t)((~leds)&0x01c0u);prev_leds=leds;}
 board_set_beep(beep);
}
/* 舵机角度输出：PD14 = TIM4_CH3（电路里 Tunnel_74 的 PinId 就是 PD14）。
   这一路现在由两个执行器共用：servo_3「液体出料闸门」和提纯段的过滤阀步进电机，
   cane_outputs 里已按"过滤阀优先"做过仲裁。本函数保留给需要单独打角度的场合。 */
void cane_servo_frame(unsigned angle)
{
 if(angle>180u)angle=180u;                 /* 上限保护：不允许超出最大角度 */
 TIM4->CCR3=500u+(uint32_t)angle*2000u/180u; /* 0 度=500us，180 度=2500us */
}
