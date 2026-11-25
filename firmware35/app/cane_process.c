/* 甘蔗榨糖五道主工序：称重→榨汁提纯→煮糖→分装→冷却。
   榨汁和提纯分两次手动推进，安全联锁优先；电机PA7、入料闸PD12、
   液体闸PD14、分装阀PD13，两只风扇随安全运行启停。 */
#include "cane.h"
#include "cane_weight.h"
#include "speech.h"
#include <stdio.h>
#include <string.h>

/* 重量内部统一用“面板值 × 10”表示（面板值 = HX711 控制盘读数，已 /420 还原）。
   两个工序共用同一份读数，只是界面挂的单位不同：
     称重页 kg，目标 900kg -> 内部 9000，达标带 ±20kg、超重 +30kg；
     分装页 g，每份 200g -> 内部 2000，单份上限 200g。 */
#define W_WEIGH_TARGET_10 9000  /* 900kg，单位 0.1kg（即面板值×10） */
#define W_TOL_10        200     /* ±20kg */
#define W_OVER_10       300     /* +30kg */
#define T_OVER_10       50
#define T_UNDER_10      100
#define SENSOR_GRACE_MS 3000u
#define BOIL_UNDER_MS   3000u  /* 低温报警宽限：只给上电/进工序 3 秒读数稳定期，原 20 秒太长 */

/* 榨汁液位 0→180cm：18 秒走满（10cm/s），到 180cm 关榨汁电机、开液体闸门。
   液位优先取超声波实测（罐满 0mm / 罐空 1800mm），仿真里罐体没被驱动时
   用时间斜坡补上，两者取大值，保证演示一定走到 180cm。 */
#define JUICE_FULL_CM    CANE_JUICE_FULL_CM
#define JUICE_RAMP_MS  18000u
#define JUICE_TANK_EMPTY_MM 1800u
/* 提纯：进工序固定 45% 浑浊度，PC13 按键开阀（PD14 舵机 90 度）后
   直流电机带过滤网运行，12 秒把浑浊度从 45% 降到 10%。 */
#define CONC_RAW        45u
#define CONC_CLEAN      CANE_CONC_CLEAN
#define FILTER_MS     12000u

/* 播报与屏幕之间的时序差。
   控制任务 50ms 一拍，显示任务周期长得多，屏幕上的数字总是晚一两拍才刷出来。
   所以"数值刚跨过阈值"就播报，实际效果是声音先到、数字还没跳上去。
   下面几个 HOLD 就是让判定先停下来等屏幕落定，再出声：
     - 榨取率到 80%（屏幕要先把液位画到 80%）
     - 提纯浓度到本轮终点（屏幕要先把浓度画到 55% 或 25%）
     - 分装重量稳定（屏幕要先把重量画到最终值）
   2 秒是显示任务最长刷新周期的若干倍，用户听感上只是"数字停住后紧跟着报"。 */
#define JUICE_SAY_HOLD_MS  2000u
#define PURIFY_SAY_HOLD_MS 2000u

/* 分装读数是一点一点加上去的，不能拿瞬时值判超重：
   1) 进工序后首次拿到有效读数，先等 FILL_START_MS（2s）避开去皮回读与首帧抖动；
   2) 之后跟踪读数，连续 FILL_STABLE_MS（2.5s）不再变化才算稳定；
   3) 只有稳定值才和 200g 目标比：正好 200g 是合格份不播报；低于 200g 播"分装偏少"；
      高于 200g 报警播"分装超重"。偏多偏少都出声，操作员靠听就知道每份是否一致。 */
#define FILL_START_MS   2000u
#define FILL_STABLE_MS  2500u
/* 提纯按"手动开阀→过滤"走：进工序浑浊度固定 45%，按 PC13 开阀后
   12 秒线性降到 10%，到 10% 就算过滤完成（参数见上面 CONC_RAW/CONC_CLEAN/FILTER_MS）。 */

/* 风扇分工（电路里只有 BK_PWM_FAN_2 一个风扇单元，接 TIM3_CH1/PA6）：
   同一个风扇用两档转速表达两件事，不会出现"两个一起转"：
     - 进风口通风：流程运行时一直低速转（FAN_VENT=60%），保持厂房通风；
     - 冷却降温：煮糖高于105度、或冷却阶段高于45度，切到全速（FAN_COOL=100%）；
    35% 在现场看太慢，观众几乎看不出它在转，所以通风档提到 60%：
    既明显在转，又能和 100% 的降温档一眼区分开。
   降温条件解除立即回落到通风低速，开/关各播报一次。 */
#define FAN_VENT   60u   /* 常开通风档（原 35% 太慢，看不出在转） */
#define FAN_COOL   100u  /* 降温全速档 */
#define BOIL_FAN_ON_10   1050  /* 煮糖 >105.0 度开风扇 */
#define BOIL_FAN_OFF_10  1050  /* 回到 105.0 度及以下关风扇 */
#define COOL_FAN_ON_10   450   /* 冷却 >45.0 度开风扇 */
#define COOL_FAN_OFF_10  450   /* 回到 45.0 度及以下关风扇 */

static const char *const stage_names[]={
 "待机","称重进料","榨汁入液","煮糖控温","糖浆分装","冷却复核","杂质提纯","糖块入库","完成"
};
const char *cane_stage_name(unsigned stage)
{return stage_names[stage<=PSTAGE_DONE?stage:0];}
/* 工序号与页号不再一一相等：提纯(6) 统一挂到榨汁页(P2)，入库(7) 挂入库页(P6)。 */
uint8_t cane_page_of_stage(unsigned stage)
{
 if(stage==PSTAGE_PURIFY_FINE)return 2u;          /* PAGE_PURIFY */
 if(stage>=PSTAGE_WEIGH&&stage<=PSTAGE_COOL)return (uint8_t)stage;
 if(stage==PSTAGE_STORE)return 6u;                /* PAGE_STORE */
 return 1u;
}

/* 状态栏一行显示的原因；语音播报用更完整的句子，见下方 alarm_speech()。 */
const char *cane_alarm_text(unsigned stage,unsigned alarm)
{
 if(alarm==PALARM_SAFETY)return "明火或人员进入";
 if(alarm==PALARM_TIMEOUT)return "本工序已超时";
 if(alarm==PALARM_NONE)return "运行正常";
 if(alarm==PALARM_SENSOR){
  if(stage==PSTAGE_WEIGH||stage==PSTAGE_FILL)return "称重未就绪";
  if(stage==PSTAGE_PURIFY||stage==PSTAGE_PURIFY_FINE)return "必需采样未就绪";
  return "测温未就绪";
 }
 if(stage==PSTAGE_WEIGH)return alarm==PALARM_OVER?"称重超重,停进料":"称重不足,继续进料";
 if(stage==PSTAGE_PURIFY||stage==PSTAGE_PURIFY_FINE)return alarm==PALARM_OVER?"浑浊度超标,请过滤":"浑浊度合格";
 if(stage==PSTAGE_BOIL)return alarm==PALARM_OVER?"温度过高,风扇全速":"温度过低,请检查加热";
 if(stage==PSTAGE_FILL)return alarm==PALARM_OVER?"分装超重,关阀":"分装不足,继续灌装";
 return alarm==PALARM_UNDER?"冷却未达标":"冷却超时";
}
static void announce(CaneState *c,const char *text,unsigned alert)
{
 CaneProcess *p=&c->proc;
 if(!text||!text[0])return;
 strncpy(p->say,text,sizeof(p->say)-1);p->say[sizeof(p->say)-1]=0;
 p->say_alert=(uint8_t)alert;p->say_seq++;
}
static void alarm_speech(CaneState *c,unsigned stage,unsigned alarm)
{
 char b[128];
 /* 明火与人员属于安全事件，由 cane_ui 统一播报并跳安全页，这里不再重复出声。 */
 if(alarm==PALARM_SAFETY)return;
 if(alarm==PALARM_SENSOR){snprintf(b,sizeof(b),"%s工序%s，请检查接线",cane_stage_name(stage),cane_alarm_text(stage,alarm));announce(c,b,1);return;}
 if(stage==PSTAGE_WEIGH&&alarm==PALARM_OVER)strcpy(b,"称重超重，已停止进料，请检查料仓下料");
 else if(stage==PSTAGE_PURIFY_FINE&&alarm==PALARM_OVER)
  /* 一次说完整：浓度 + 超标准 + 请再次提纯。
     按下一步重起一轮时不再重复播这句（见 cane_process_next）。 */
  snprintf(b,sizeof(b),"提纯结束，杂质浓度百分之%u，高于标准百分之%u，请再次提纯",c->proc.conc,c->proc.t_conc);
 else if(stage==PSTAGE_PURIFY&&alarm==PALARM_OVER)strcpy(b,"杂质浓度超限，请检查过滤装置");
 else if(stage==PSTAGE_BOIL&&alarm==PALARM_OVER)strcpy(b,"糖浆温度过高，散热风扇已全速，请检查加热");
 else if(stage==PSTAGE_BOIL&&alarm==PALARM_UNDER)strcpy(b,"糖浆温度过低，请检查加热功率");
 else if(stage==PSTAGE_FILL&&alarm==PALARM_OVER)
  /* 带实际重量播报：操作员要知道这一份到底多了多少，才好把下一份调回来。 */
  snprintf(b,sizeof(b),"分装超重，本份糖浆%u克，超过%u克上限，分装阀已关闭",(unsigned)(c->sample.weight10/10u),(unsigned)(c->proc.t_fill_max10/10));
 else if(stage==PSTAGE_FILL&&alarm==PALARM_UNDER)
  /* 偏少同样要报，带实际克数，操作员才知道差多少、要不要补。 */
  snprintf(b,sizeof(b),"分装偏少，本份糖浆%u克，低于%u克标准，请检查分装阀",(unsigned)(c->sample.weight10/10u),(unsigned)(c->proc.t_fill10/10));
 else snprintf(b,sizeof(b),"%s工序报警，%s",cane_stage_name(stage),cane_alarm_text(stage,alarm));
 announce(c,b,1);
}
unsigned cane_concentration(uint16_t distance_mm)
{
 int v=(1200-(int)distance_mm)*100/1000;
 return (unsigned)(v<0?0:v>100?100:v);
}
void cane_process_enter(CaneState *c,unsigned stage,uint32_t now)
{
 CaneProcess *p=&c->proc;
 p->stage=(uint8_t)stage;p->entered=now;p->progress_at=now;p->phase_ticks=0;p->settle_at=0;p->alarm=PALARM_NONE;
 p->feeder=0;p->valve=0;p->stir=0;
 /* 过滤阀是"一个人按一下才开"的手动阀，离开提纯工序必须回 0 度，
     否则 PD14 会一直停在 90 度，把后面的阀门动作带偏。 */
 if(stage!=PSTAGE_PURIFY_FINE){p->filter_req=0;p->filter_open=0;}
 p->tare_wait=0;p->cool_said=0;p->cool_fan=0;p->boil_fan=0;
 if(stage==PSTAGE_WEIGH){p->fill_count=0;p->juice_level=0;p->juice_cm=0;p->conc=0;p->weigh_said=0;
  /* 开始称重先做一次 HX711 去皮复位：秤台当前重量记为新的零点，
     屏幕读数立即回 0，再让采样任务回读确认，然后才允许进料。 */
  p->tare_serial=cane_tare_serial();p->tare_wait=1;cane_request_tare_loud(1);}
 if(stage==PSTAGE_PURIFY){
  /* 进榨汁先不转电机：液位从 0 起，由操作员按"继续运行"再启动榨汁机，
     这样演示节奏是"先看到 0，再看着液位往上涨"。 */
  p->juice_level=0;p->juice_cm=0;p->juice_done=0;p->run=0;p->settle_at=0;
  announce(c,"已进入榨汁环节，按继续运行启动榨汁机",0);
 }
 if(stage==PSTAGE_PURIFY_FINE){
  /* 进提纯固定 45% 浑浊度，等操作员按 PC13 手动开过滤阀。 */
  p->conc=CONC_RAW;p->conc_from=CONC_RAW;p->purify_done=0;
  p->filter_req=0;p->filter_open=0;p->filter_done=0;
  announce(c,"杂质过多，请手动开启提纯过滤",0);
 }
 if(stage==PSTAGE_BOIL){
  /* 煮糖从常温起：进工序就把"现在多少度、该升到多少度"一次报清楚，
     操作员不用先看屏幕再判断要不要开加热。
     温度还没采到时不念假数字，提示检查测温模块。 */
  char cur[24],tgt[24],b[128];
  p->boil_say=0;
  /* 这里先播一次（常规路径）；boil_intro 归 0，让流程第一拍再补一次兜底，
     同样的文案会被语音队列的 4 秒去重挡掉，不会重复出声。 */
  p->boil_intro=0;
  cane_temp_speech(tgt,sizeof(tgt),p->t_boil10);
  if(c->sample.valid&CANE_IR){
   cane_temp_speech(cur,sizeof(cur),(int32_t)c->sample.object10);
   snprintf(b,sizeof(b),"当前温度%s，应当上升到%s",cur,tgt);
  }else snprintf(b,sizeof(b),"当前温度未就绪，应当上升到%s，请检查测温模块",tgt);
  announce(c,b,0);
 }
 if(stage==PSTAGE_STORE){p->store_say=0;announce(c,"已进入糖块入库，正在检测储存仓温湿度",0);}
 /* 分装进工序只去皮一次：这是全程唯一一次出声的去皮。 */
 if(stage==PSTAGE_FILL){
  p->fill_count=0;p->tare_serial=cane_tare_serial();p->tare_wait=1;cane_request_tare_loud(1);
  p->fill_done=0;p->fill_start_at=0;p->fill_last10=0;
 }
}
void cane_process_init(CaneProcess *p,uint32_t now)
{
 memset(p,0,sizeof(*p));
 p->t_weigh10=W_WEIGH_TARGET_10; /* 称重目标 900kg */
 p->t_fill10=2000;    /* 每份 200g */
 p->t_fill_max10=2000;/* 分装单份上限 200g，超过立刻关阀报警 */
 p->t_boil10=1020;    /* 煮糖 102.0度 */
 p->t_cool10=450;     /* 冷却到 45.0度 */
 p->t_conc=30;        /* 浑浊度上限 30% */
 p->fill_total=3;
 p->stage=PSTAGE_WEIGH;p->run=0;p->entered=now;
}
/* 每50ms由控制任务调用一次；返回值通过 CaneState 的执行器字段生效。 */
void cane_process_step(CaneState *c,uint32_t now)
{
 CaneProcess *p=&c->proc;
 const CaneSample *s=&c->sample;
 unsigned alarm=PALARM_NONE;
 uint32_t elapsed=now-p->entered,dt=now-p->progress_at;
 p->progress_at=now;
 int32_t weight=(s->valid&CANE_WEIGHT)?(int32_t)s->weight10:-1;
 int32_t temp=(s->valid&CANE_IR)?(int32_t)s->object10:-32768;


 /* 每个周期先关闭工序动作，防止残留；通风由cane_apply统一决定。 */
 p->feeder=0;p->valve=0;p->stir=0;


 /* 明火与人员是硬停机条件，任何工序都立即生效。 */
 if(s->fire||s->person){
  if(p->alarm!=PALARM_SAFETY)alarm_speech(c,p->stage,PALARM_SAFETY);
  p->alarm=PALARM_SAFETY;p->run=0;p->settle_at=0;return;
 }
 /* 环境联锁或硬故障停机后，必须由操作员重新启动。
    只认红外/称重/反馈超时这几项，测距和霍尔缺失不停线（见 CANE_HARD_FAULTS）。 */
 if(c->environment_faults||(c->faults&CANE_HARD_FAULTS)){p->run=0;p->settle_at=0;return;}
 if(p->alarm==PALARM_SAFETY)p->alarm=PALARM_NONE;
 if(p->tare_wait&&(s->valid&CANE_WEIGHT)&&s->tare_serial!=p->tare_serial)p->tare_wait=0;
 /* 流程里补报煮糖温度：提纯工序按"下一步"推进到煮糖时，
     cane_process_next 紧接着发的"下一步，已手动进入煮糖"会盖掉进入工序那条播报，
     所以进煮糖后的第一拍在这里补一次"当前温度/应当上升到"。
     放在暂停判断之前，流程是运行还是暂停都照样播；有真实红外读数才念数字，
     读不到就等宽限期过后提示检查测温模块，绝不念假数字。 */
 if(p->stage==PSTAGE_BOIL&&!p->boil_intro){
  char cur[24],tgt[24],m[128];
  cane_temp_speech(tgt,sizeof(tgt),p->t_boil10);
  if(temp!=-32768){
   p->boil_intro=1;
   cane_temp_speech(cur,sizeof(cur),(int32_t)temp);
   snprintf(m,sizeof(m),"当前温度%s，应当上升到%s",cur,tgt);
   announce(c,m,0);
  }else if(elapsed>SENSOR_GRACE_MS){
   p->boil_intro=1;
   snprintf(m,sizeof(m),"当前温度未就绪，应当上升到%s，请检查测温模块",tgt);
   announce(c,m,0);
  }
 }
 if(!p->run){
  /* 未启动或暂停时只冻结动作与工序推进，超限判定照常执行：
     重量加到阈值、温度越限，蜂鸣器与语音必须照样报警，不能随流程一起停。 */
  if(weight<0)alarm=(elapsed>SENSOR_GRACE_MS)?PALARM_SENSOR:PALARM_NONE;
  else if(p->stage==PSTAGE_WEIGH&&weight>p->t_weigh10+W_OVER_10)alarm=PALARM_OVER;
  else if(p->stage==PSTAGE_FILL&&weight>p->t_fill_max10)alarm=PALARM_OVER;
  else if(p->stage==PSTAGE_BOIL&&temp!=-32768){
   /* 低温不算报警：煮糖从常温起步，升温是正常过程。 */
   if(temp>p->t_boil10+T_OVER_10)alarm=PALARM_OVER;
  }
  if(alarm!=PALARM_NONE&&alarm!=p->alarm)alarm_speech(c,p->stage,alarm);
  else if(alarm==PALARM_NONE&&p->alarm!=PALARM_NONE)announce(c,"报警已解除，流程继续",0);
  p->alarm=(uint8_t)alarm;p->settle_at=0;return;
 }
 if(dt>200u)dt=0;
 if(p->stage==PSTAGE_PURIFY||p->stage==PSTAGE_PURIFY_FINE){
  unsigned ticks=p->phase_ticks+dt;p->phase_ticks=(uint16_t)(ticks>20000u?20000u:ticks);
 }

 switch(p->stage){
 case PSTAGE_IDLE:
  p->run=0;
  break;

 case PSTAGE_WEIGH:
  /* 称重接近目标关闭入料门，超过上限停止并报警。 */
  if(p->tare_wait){
   if((s->valid&CANE_WEIGHT)&&s->tare_serial!=p->tare_serial){p->tare_wait=0;p->settle_at=0;}
   else if(elapsed>2000u){p->tare_wait=0;p->settle_at=0;}
  }
  else if(weight<0){if(elapsed>SENSOR_GRACE_MS)alarm=PALARM_SENSOR;p->settle_at=0;}
  else if(weight>p->t_weigh10+W_OVER_10){alarm=PALARM_OVER;p->feeder=0;p->settle_at=0;}
  else if(weight>=p->t_weigh10-W_TOL_10){
   p->feeder=0;
   if(!p->settle_at)p->settle_at=now;
   /* 到 900kg 播一次称重完成；称重达标只停止进料，等操作员按下一步。 */
   if(!p->weigh_said){p->weigh_said=1;announce(c,"单次榨汁甘蔗称重完成",0);}
  }else{p->feeder=1;p->settle_at=0;}
  break;

 case PSTAGE_PURIFY:{
  /* 榨汁：液位 0→180cm。液位取"超声波实测"与"时间斜坡"的较大值：
     罐空 1800mm 对应 0cm，罐满 0mm 对应 180cm；仿真里罐内液面不会自己动，
     用 18 秒斜坡保证演示一定走到 180cm，现场若超声波真测到液面就按实测走。 */
  uint32_t t=p->phase_ticks;
  unsigned sim=(unsigned)((t>=JUICE_RAMP_MS?JUICE_RAMP_MS:t)*JUICE_FULL_CM/JUICE_RAMP_MS);
  unsigned meas=0;
  if(s->valid&CANE_DISTANCE){
   unsigned d=s->distance_mm;
   unsigned cm=(d<JUICE_TANK_EMPTY_MM?(JUICE_TANK_EMPTY_MM-d):0u)/10u;
   meas=cm>JUICE_FULL_CM?JUICE_FULL_CM:cm;
  }
  p->juice_cm=(uint8_t)(sim>meas?sim:meas);
  p->juice_level=(uint8_t)((unsigned)p->juice_cm*100u/JUICE_FULL_CM);  /* 百分比，给状态灯用 */
  if(p->juice_cm<JUICE_FULL_CM){p->feeder=1;p->stir=70;p->settle_at=0;}
  else{
   /* 到 180cm：关榨汁直流电机，开液体闸门放汁，等屏幕落定再播报一次。 */
   p->feeder=0;p->valve=1;p->stir=0;
   if(!p->juice_done){
    if(!p->settle_at)p->settle_at=now;
    else if(now-p->settle_at>=JUICE_SAY_HOLD_MS){
     p->juice_done=1;
     announce(c,"已经达到最大榨汁液位，关闭榨汁",0);
    }
   }
  }
  break;}

 case PSTAGE_PURIFY_FINE:{
  /* 提纯：进工序固定 45% 浑浊度，先播"杂质过多，请手动开启提纯过滤"，
     等操作员按 PC13。按下后 PD14 的舵机/步进电机转到 90 度开过滤阀
     （角度写进 TIM4_CH3，定时器持续输出，阀位自己咬住），
     同一拍播报"已打开过滤阀门，开始过滤"，然后 PA7 直流电机带过滤网运行，
     浑浊度 45%→10%，到 10% 停电机并播报过滤完成。
     c->user_next 是"按下一步"的一次性标记：提纯段按下一步本来就是要往下走，
     阀还没开就先替他开阀，然后由 cane_process_next 的 filter_done 判定挡住跳工序，
     等过滤跑到 10% 再按一次才进煮糖。 */
  if(!p->filter_open&&(p->filter_req||c->user_next)){
   p->filter_req=0;p->filter_open=1;p->phase_ticks=0;p->progress_at=now;
   announce(c,"已打开过滤阀门，开始过滤",0);
  }
  p->filter_req=0;
  if(!p->filter_open){
   p->conc=CONC_RAW;p->stir=0;p->settle_at=0;   /* 阀没开：浑浊度停在 45%，电机不转 */
  }else{
   uint32_t ft=p->phase_ticks;
   uint32_t drop=(ft>=FILTER_MS?FILTER_MS:ft)*(CONC_RAW-CONC_CLEAN)/FILTER_MS;
   p->conc=(uint8_t)(CONC_RAW-drop);
   if(p->conc>CONC_CLEAN){p->stir=60;}
   else{
    p->stir=0;
    if(!p->filter_done){p->filter_done=1;announce(c,"浑浊度下降到10%，过滤完成",0);}
   }
  }
  break;}

 case PSTAGE_BOIL:{
  /* 煮糖：进工序先提示"升到102度"，到 102 度启动直流电机模拟搅拌。
     低温不再报警——演示本来就从常温 36.5 度起步，升温过程不是异常。 */
  char b[128];
  if(temp==-32768){if(elapsed>SENSOR_GRACE_MS)alarm=PALARM_SENSOR;p->settle_at=0;p->stir=0;}
  else{
   if(temp>p->t_boil10+T_OVER_10){alarm=PALARM_OVER;p->settle_at=0;p->stir=0;}
   else if(temp>=p->t_boil10){
    if(!p->settle_at)p->settle_at=now;
    p->stir=50;                       /* 到 102 度：直流电机启动，模拟煮糖搅拌 */
    if(!p->boil_say){
     p->boil_say=1;
     snprintf(b,sizeof(b),"糖浆已达%d点%d度，搅拌电机已启动",(int)(temp/10),(int)(temp%10));
     announce(c,b,0);
    }
   }else{p->settle_at=0;p->stir=0;}   /* 还没到 102 度：只等升温，不报警 */
   /* 开：温度高于 105 度；关：回到 105 度及以下。 */
   if(!p->boil_fan&&temp>BOIL_FAN_ON_10){
    p->boil_fan=1;
    snprintf(b,sizeof(b),"煮糖温度%d点%d度，高于%d点%d度，风扇已提速降温",(int)(temp/10),(int)(temp%10),(int)(BOIL_FAN_ON_10/10),(int)(BOIL_FAN_ON_10%10));
    announce(c,b,0);
   }else if(p->boil_fan&&temp<=BOIL_FAN_OFF_10){
    p->boil_fan=0;
    snprintf(b,sizeof(b),"煮糖温度已回到%d点%d度，风扇已恢复通风",(int)(temp/10),(int)(temp%10));
    announce(c,b,0);
   }
  }
  break;}

 case PSTAGE_FILL:{
  /* 进分装页只去皮一次（enter 里已请求），之后不再去皮，读数就是累计糖浆重量。
     读数是一点一点加上去的，所以判定分三步走（见 FILL_START_MS / FILL_STABLE_MS）：
       1) 首次拿到有效读数后先等 2 秒，避开去皮回读和首帧抖动；
       2) 之后每拍比较读数，只要还在变就重新计时，连续 2.5 秒不变才算稳定；
       3) 拿稳定值对照 200g 标准：正好 200g 是合格份，不报警；
          高于 200g 报"分装超重"（PALARM_OVER），低于 200g 报"分装偏少"（PALARM_UNDER），
          两档都置报警位：屏幕红、蜂鸣器响、语音出声，一直保持到操作员处理。
     报警必须每周期持续置位，否则下一周期会被"报警已解除"冲掉。 */
  if(weight<0){
   if(elapsed>SENSOR_GRACE_MS)alarm=PALARM_SENSOR;
   p->settle_at=0;p->fill_start_at=0;
  }
  else if(p->tare_wait){
   if(s->tare_serial!=p->tare_serial){p->tare_wait=0;p->settle_at=0;}
   else if(elapsed>2000u){p->tare_wait=0;p->settle_at=0;}
  }
  else{
   /* 第一次拿到有效读数：记时刻，往后才开始算稳定 */
   if(!p->fill_start_at){p->fill_start_at=now;p->fill_last10=weight;p->settle_at=0;}
   if(weight!=p->fill_last10){p->fill_last10=weight;p->settle_at=now;} /* 还在涨：重新计时 */
   else if(!p->settle_at)p->settle_at=now;                            /* 持平：开始计时 */
   /* 到达或超过单份上限先关阀，稳定后再播报，避免读数还在爬就喊超重 */
   if(weight>=p->t_fill10||p->fill_done)p->valve=0;
   else p->valve=1;
   if(p->fill_done){
    /* 已判定过：偏多偏少都保持报警，直到操作员处理（按下一步重进分装页报警才清）。 */
    if(weight>p->t_fill_max10)alarm=PALARM_OVER;
    else if(weight<p->t_fill10)alarm=PALARM_UNDER;
   }else if(weight>0&&now-p->fill_start_at>=FILL_START_MS&&now-p->settle_at>=FILL_STABLE_MS){
    /* 稳定判定：屏幕出现数据后先等 2 秒（FILL_START_MS），
       再要求连续 2.5 秒读数不变（FILL_STABLE_MS），才拿这个稳定值对照 200g 标准。 */
    p->fill_done=1;p->valve=0;
    p->fill_count++;
    if(weight>p->t_fill_max10){
     alarm=PALARM_OVER;   /* 文案由 alarm_speech 统一出，这里不能再 announce，否则播两遍 */
    }else if(weight<p->t_fill10){
     /* 稳定值低于 200g：这阀没灌够就停了，同样算不合格，必须报警不只是提示。
        正好等于 200g 是合格份，不报警。 */
     alarm=PALARM_UNDER;
    }
   }
  }
  break;}

 case PSTAGE_COOL:{
  /* 同一个风扇（BK_PWM_FAN_2）平时低速通风；温度判到要降温就切全速：
     高于 45 度切全速（播一次），回到 0~45 度回落通风（播一次）。 */
  char b[128];
  if(temp==-32768){if(elapsed>SENSOR_GRACE_MS)alarm=PALARM_SENSOR;p->settle_at=0;}
  else{
   if(temp>COOL_FAN_ON_10){
    p->settle_at=0;
    if(!p->cool_fan){
     p->cool_fan=1;
     snprintf(b,sizeof(b),"糖浆温度%d点%d度，高于%d点%d度，风扇已提速降温",(int)(temp/10),(int)(temp%10),(int)((int32_t)COOL_FAN_ON_10/10),(int)((int32_t)COOL_FAN_ON_10%10));
     announce(c,b,0);
    }
   }else{
    if(!p->settle_at)p->settle_at=now;
    if(p->cool_fan){
     p->cool_fan=0;
     snprintf(b,sizeof(b),"糖浆温度已降到%d点%d度，风扇已恢复通风",(int)(temp/10),(int)(temp%10));
     announce(c,b,0);
    }
    /* 冷却达标不自动回批，下一批只在手动下一步时记数。 */
   }
  }
  break;}

 case PSTAGE_STORE:
  /* 糖块入库：PA0 上的 DHT11_1 由环境采集任务持续读取，
     界面直接显示储存仓温湿度，本工序不需要额外动作。 */
  p->feeder=0;p->valve=0;p->stir=0;
  break;

 default:
  p->run=0;
  break;
 }

 /* 报警时停住进料与阀门；超温报警保留风扇全速。 */
 if(alarm!=PALARM_NONE&&alarm!=PALARM_OVER){p->feeder=0;p->valve=0;p->stir=0;}
 if(alarm!=p->alarm&&alarm!=PALARM_NONE)alarm_speech(c,p->stage,alarm);
 else if(alarm==PALARM_NONE&&p->alarm!=PALARM_NONE)announce(c,"报警已解除，流程继续",0);
 p->alarm=(uint8_t)alarm;

}

/* 本地按键、屏幕和云命令共用的唯一跨工序入口；期望工序防止重复命令连跳。 */
int cane_process_next(CaneState *c,unsigned expected,uint32_t now)
{
 CaneProcess *p=&c->proc;unsigned next;char b[96];uint8_t said;
 if(expected<PSTAGE_WEIGH||expected>PSTAGE_STORE)return 1;
 if(p->stage!=expected)return 4;
 if(c->sample.fire||c->sample.person||c->environment_faults)return 2;
 /* 温湿度探头读不到（AG_FAULT_AIR）不算联锁：它已经在 cane_apply 里被剔出
    environment_faults，只做报警提示。这里如果还按 faults 判，电机照样会被停。 */
 if(((c->faults&CANE_HARD_FAULTS)!=0)||now-c->sample.at>3000u)return 3;
 if(expected==PSTAGE_WEIGH&&p->tare_wait)return 3;
 /* 过滤没走完不许进煮糖：浑浊度必须真正降到 10%。 */
 if(expected==PSTAGE_PURIFY_FINE&&!p->filter_done){
  announce(c,"浑浊度还没降到10%，请先按PC13开启过滤阀门",1);return 5;
 }
 /* 工序顺序：称重→榨汁→提纯→煮糖→分装→冷却→糖块入库→下一批称重。 */
 if(expected==PSTAGE_COOL)next=PSTAGE_STORE;
 else if(expected==PSTAGE_PURIFY)next=PSTAGE_PURIFY_FINE;
 else if(expected==PSTAGE_PURIFY_FINE)next=PSTAGE_BOIL;
 else if(expected==PSTAGE_STORE){next=PSTAGE_WEIGH;p->batch++;}
 else next=expected+1u;
 /* 进工序的专属提示（煮糖温度、提纯开阀、入榨启动等）比通用导航更有用，
     而且 announce 只有一个播报槽：同一拍连发两条，后一条会把前一条冲掉，
     煮糖那次丢的正是"当前温度…应当上升到…"。所以进工序已经出过声就不再补导航句。 */
 said=p->say_seq;
 cane_process_enter(c,next,now);c->page=cane_page_of_stage(next);
 if(p->say_seq==said){
  snprintf(b,sizeof(b),expected==PSTAGE_STORE?"已手动开始下一批，进入%s":"下一步，已手动进入%s",cane_stage_name(next));
  announce(c,b,0);
 }
 return 0;
}
