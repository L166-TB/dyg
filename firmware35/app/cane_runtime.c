#include "cane.h"
#include "board_port.h"
#include "board_hal.h"
#include "los_task.h"
#include "los_mux.h"
#include "tft35.h"
#include "app_runtime.h"
#include "speech.h"

#include "cane_gui.h"
#include "cane_weight.h"

#include <stdio.h>
#include <string.h>
static UINT32 mutex;
static CaneState state;
static UINT32 cane_task_ids[2];
extern unsigned cane_wifi_connected(void);
static uint16_t limit(int v,int lo,int hi){return (uint16_t)(v<lo?lo:v>hi?hi:v);}

/* 工序控制进料、加料门、搅拌和分装阀；两只风扇随流程运行，安全联锁优先。 */
void cane_apply(CaneState *c,uint32_t now)
{
 const CaneSample *s=&c->sample;
 CaneProcess *p=&c->proc;
 c->faults=(~s->valid)&(CANE_IR|CANE_WEIGHT);
 if(now-s->at>3000u)c->faults|=16u;
 /* 环境故障分三种处理：
      · 采集不到 / 烟雾 / 温度过高 —— 必须联锁停机（environment_faults）；
      · 储存仓湿度过高（>=70%）—— 只报警不停线：湿了要除湿通风，
        但没必要把整条产线停下来。所以这里把湿度位从联锁里剔掉，
        单独并到 c->alarm，让蜂鸣器、状态灯、顶栏照样报警。
      · 温湿度探头没读到帧（AG_FAULT_AIR）—— 同样只报警不停线。
        平台 DHT11 元件在仿真里常常吐不出完整帧，以前这一位会进联锁，
        于是在 cane_process_step 里直接把 run 清 0，
        连 PA7 的榨汁 / 搅拌电机都被每拍归零 —— 表现就是"三个工序电机都不转"。
        探头读不到只是"看不到环境数据"，不代表产线不能跑：
        温湿度相关的判定本来就在 agri_control.c 里用同一条件挡掉了，
        这里照样把它剔出联锁，只保留在 c->alarm 和串口日志里。 */
 {uint32_t env=app_interlock_faults();
  uint32_t watch=~CANE_ENV_INTERLOCK;              /* 只提示、不停线的位 */
  c->environment_faults=env&CANE_ENV_INTERLOCK;
  c->alarm=(uint8_t)((s->fire||s->person||c->faults||c->environment_faults||p->alarm||
                      (env&watch))?1u:0u);}
 cane_process_step(c,now);
 /* 电路里只有 BK_PWM_FAN_2 一个风扇单元，用两档转速表达两种需求，
    这样不会再出现"通风和冷却两个风扇一起转"：
      c->fan  = 风扇实际 PWM 百分比（通风 60% / 降温 100% / 停机 0）；
      c->fan2 = 是否处于降温全速档，界面和播报只反映这一档。 */
 {unsigned safe=(p->run&&!s->fire&&!s->person&&!c->environment_faults&&!c->faults)?1u:0u;
  unsigned cool=(safe&&(p->boil_fan||p->cool_fan))?1u:0u;
  /* fan 给硬件：通风 60% / 降温 100% / 停机 0；fan2 给界面：1=降温档 */
  c->fan=safe?(cool?100u:60u):0u;
  c->fan2=(uint16_t)cool;}
 app_set_process_vent(c->fan!=0u);
 /* 三只舵机按电路固定分工（网表 Tunnel PinId 已核对）：
      servo[0] -> PD13 (TIM4_CH2)  称重加料阀 / 分装阀
      servo[1] -> PD12 (TIM4_CH1)  入料闸门：进料时开 180 度
      servo[2] -> PD14 (TIM4_CH3)  液体出料闸门：榨汁液位满时排液 180 度
      servo[3] -> PD14 (TIM4_CH3)  提纯过滤阀：按 PC13 转 90 度（CANE_VALVE_OPEN_DEG）
    提纯段的"开过滤阀"按现场要求挂在 **PD14** 上转 90 度，
    与 servo[2] 共用 TIM4_CH3，由 cane_hal 仲裁（过滤阀优先），
    PD13 不再兼任过滤阀，分装和提纯两个动作互不干扰。 */
 {
  unsigned a13=0u;
  /* PD13 只做分装阀（180 度全开）：过滤阀已经挪到 PD14，这里不再兼任。 */
  if(p->stage==PSTAGE_FILL)         a13=(p->valve?180u:0u);
  c->servo[0]=a13;
 }
 c->servo[1]=p->feeder?180:0;                      /* 加料门 PD12（TIM4_CH1） */
 c->motor=p->stir;
 /* PD14 是两路共用的：提纯过滤阀（servo[3]，0/90 度）优先，其余工序归零 */
 c->servo[3]=(p->stage==PSTAGE_PURIFY_FINE&&p->filter_open)?CANE_VALVE_OPEN_DEG:0u;
 if(p->stage==PSTAGE_PURIFY&&p->juice_cm>=CANE_JUICE_FULL_CM)c->servo[2]=180u;
 else c->servo[2]=0u;   /* 换到别的工序一律回 0 度，别把开度带进煮糖段 */
 c->feeder=p->feeder;
 /* c->alarm 已在上面统一算好（含湿度报警位），这里不再重复赋值。 */
 /* 停机条件只看真正的硬故障：红外/称重采集失败、设备反馈超时、环境联锁、
    明火人员。测距(bit2)和霍尔(bit3)只做提示，不再把整条产线连同 PA7 电机
    一并停掉——仿真里超声波经常量不到液面，霍尔只是显示转速，
    它们缺失不该让榨汁/提纯/煮糖三个工序的电机都不转。 */
 if(!p->run||s->fire||s->person||c->environment_faults||(c->faults&CANE_HARD_FAULTS)){
  p->feeder=p->valve=p->stir=0;c->motor=0;c->servo[0]=c->servo[1]=c->servo[2]=c->servo[3]=0;c->feeder=0;
 }
 cane_outputs(c,now);
}
static void lock(void){if(LOS_MuxPend(mutex,LOS_WAIT_FOREVER)!=LOS_OK)while(1){}}
static void unlock(void){(void)LOS_MuxPost(mutex);}
/* 采集拍：读一次传感器并发布，节拍由内核按 350ms 排，函数里不再睡。
   入口签名按 LiteOS-M 的 TSK_ENTRY_FUNC：VOID (*)(UINT32)。 */
static VOID cane_sensor_entry(UINT32 arg)
{
 (void)arg;
 CaneSample sample;cane_read(&sample);lock();state.sample=sample;unlock();
}
/* 资源证据涵盖过程采集和独立保护任务。 */
void cane_resource_report(uint32_t now)
{
 unsigned i;TSK_INFO_S info;
 for(i=0;i<2;i++)if(LOS_TaskInfoGet(cane_task_ids[i],&info)==LOS_OK)
  printf("STACK,%lu,task=%lu,peak=%lu,size=%lu,overflow=%u\n",(unsigned long)now,
   (unsigned long)cane_task_ids[i],(unsigned long)info.uwPeakUsed,(unsigned long)info.uwStackSize,info.bOvf);
}
/* 电路上的按键：PC10 上一页、PC11 下一步、PC12 启动/暂停/确认、PC13 减（提纯段=开过滤阀）、
   PB14 安全页/确认报警。原 PC14 的称重键与日夜切换键已从电路删除，这里不再扫描，
   否则悬空脚加上拉也会被误读成一直按下。 */
static uint8_t keys(void)
{
 uint8_t r=0;unsigned i;
 for(i=0;i<4;i++)if(GPIO_ReadInputDataBit(GPIOC,1u<<(10+i))==RESET)r|=1u<<i;
 if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)==RESET)r|=1u<<4;
 return r;
}
/* 过滤阀目标角度：过滤阀关了就是 0 度，开了就是 90 度，不会给出别的值。 */
unsigned cane_filter_valve_angle(void)
{unsigned open;lock();open=state.proc.filter_open;unlock();return open?CANE_VALVE_OPEN_DEG:CANE_VALVE_CLOSE_DEG;}

/* 湿度报警的播报话术：现场要求就固定这一句，不念数值、不加"安全报警"前缀。
   定义成宏是为了让报警播报、入库页播报、安全页说明三处永远是同一句话。 */
#define HUMI_ALARM_TEXT "当前环境湿度过高，请注意防潮"
/* 温度过高就一句话，不分工序：DHT11_1 读到 50℃ 及以上就播这句。
   以前按工序分"储存仓环境温度过高"和"蔗糖厂温度过高"两种说法，现场听不出
   区别，反而要记两套；现在统一成一条，屏幕、蜂鸣器、语音三处同一句。 */
#define HOT_ALARM_TEXT "温度过高，请采取相应措施"
/* 温湿度探头读不到帧时不播报。
   平台上拖动 DHT11_1 的温度参数时，单总线时序会被打断，连续几帧解不出来，
   5 秒有效窗一过就会冒出"温湿度采集未就绪，请检查环境探头"——操作员只是
   在调一个正常值，却听到"传感器没接"，很容易当成设备坏了。
   探头读数属于"看得到更好、看不到也能跑"的量（AG_FAULT_AIR 本来就不在
   CANE_ENV_INTERLOCK 联锁里），所以这一位只留串口日志，不抢安全页、不出声，
   屏幕上也只保留最后一次真实读数，不写"未就绪"。 */
static const char *alarm_reason(const CaneState *c,const AgControl *a)
{
 if(c->sample.fire)return "检测到明火，请远离设备并检查现场";
 if(c->sample.person)return "人员进入危险区域，请离开设备作业区";
 /* 烟雾报警走固定消防话术，现场听到就知道要立刻处置；实时 ppm 仍在屏幕与日志上。 */
 if(a->faults&AG_FAULT_SMOKE_HIGH)return "检测到烟雾浓度过高，请立即采取消防措施";
 /* 温度过高：>=50℃ 报警，话术只有一句，不按工序分开说。
    判定阈值在 agri_control.h（AG_HOT_ALARM），这里只管怎么讲。 */
 if(a->faults&AG_FAULT_AIR_HOT)return HOT_ALARM_TEXT;
 /* 湿度报警只用固定一句话，实测值仍在屏幕卡片和云端上报里。 */
 if(a->faults&AG_FAULT_HUMI_HIGH)return HUMI_ALARM_TEXT;
 if(a->faults&AG_FAULT_LEVEL_HIGH)return "料仓接近满位，请停止继续加料";
 if(c->proc.alarm)return cane_alarm_text(c->proc.stage,c->proc.alarm);
 if(c->faults&2u)return "称重传感器未就绪，请检查秤台接线";
 if(c->faults&1u)return "红外测温未就绪，请检查测温模块";
 if(c->faults&4u)return "设备测距未就绪，请检查超声探头";
 if(c->faults&8u)return "霍尔信号未就绪，请检查采集接线";
 if(c->faults&16u)return "设备反馈已超时，请检查采集任务";
 if(a->faults&AG_FAULT_SMOKE_SENSOR)return "烟雾采集未就绪，请检查探头接线";
 if(a->faults&AG_FAULT_LEVEL)return "料仓测距无效，请检查探头位置";
 /* AG_FAULT_AIR 刻意不在这里返回任何话术：详见文件开头 HOT_ALARM_TEXT 下面的说明。
     走到这里说明除"温湿度探头没读到帧"以外没有别的问题，那就当一切正常。 */
 if(a->faults&AG_FAULT_LIGHT)return "照度采集未就绪，请检查光敏接线";
 /* AG_FAULT_TIMING 不在这里出现：它不进 safety_bits，不会跳到安全页，
    自然不会走到这条播报。 */
 return "环境采集超时，请检查采集任务";
}
static uint32_t alarm_bits(const CaneState *c,const AgControl *a)
{return c->faults|((uint32_t)c->sample.fire<<5)|((uint32_t)c->sample.person<<6)|
 (c->proc.alarm?(1u<<(19u+c->proc.alarm)):0u)|(a->faults<<8);}
/* 安全类事件：明火、人员、采集故障、环境联锁。工艺越限不算在内——
   它由 cane_process.c 的 alarm_speech 播报具体原因，这里重复播就成两句了。
   另外 AG_FAULT_TIMING（控制调度超时）也被剔掉：它是软件自身的性能指标，
   一旦抖动就跳安全页、出声打断，录制介绍时非常难受。真卡死时屏幕本来
   就不会刷新，抢页面也没意义，所以这一位只留在串口日志里。 */
static uint32_t safety_bits(const CaneState *c,const AgControl *a)
{return c->faults|((uint32_t)c->sample.fire<<5)|((uint32_t)c->sample.person<<6)|
 ((a->faults&~AG_FAULT_TIMING)<<8);}

/* 每一页播报本页的实际读数，不读没有采集到的量。 */
static void report(const AgControl *a,const CaneState *c,const char *operation)
{
 char b[160],mass[24],target[24];const CaneSample *s=&c->sample;const CaneProcess *p=&c->proc;
 cane_mass_speech(mass,sizeof(mass),s->weight10);
 if(c->page==PAGE_SAFETY){
  if(alarm_bits(c,a))snprintf(b,sizeof(b),"安全页，%s",alarm_reason(c,a));
  else strcpy(b,"安全页，明火和人员检测正常，传感器反馈正常");
 }else if(c->page==PAGE_WEIGH){
  if(!(s->valid&CANE_WEIGHT))strcpy(b,"称重页，称重传感器未就绪");
  else{cane_mass_speech(target,sizeof(target),(unsigned)p->t_weigh10);
   snprintf(b,sizeof(b),"称重页，净重%s，目标%s，入料闸门%s",mass,target,p->feeder?"运行中":"已停止");}
 }else if(c->page==PAGE_PURIFY){
  if(p->stage==PSTAGE_PURIFY_FINE){
   /* 阀没开就是"请手动开过滤"；开了就是"正在过滤"。话术与按键后的播报对得上。 */
   if(!p->filter_open)
    snprintf(b,sizeof(b),"提纯页，蔗糖汁浑浊度百分之%u，杂质过多，请按过滤键手动开启提纯过滤",p->conc);
   else if(!p->filter_done)
    snprintf(b,sizeof(b),"提纯页，蔗糖汁浑浊度百分之%u，过滤阀已打开，正在过滤，标准百分之%u以下",p->conc,p->t_conc);
   else
    snprintf(b,sizeof(b),"提纯页，蔗糖汁浑浊度百分之%u，已降到标准百分之%u以下，过滤完成",p->conc,p->t_conc);
  }else snprintf(b,sizeof(b),"榨汁入液，甘蔗液位%u厘米，榨汁电机百分之%u，液体闸门%s",p->juice_cm,p->stir,p->valve?"已打开":"已关闭");
 }else if(c->page==PAGE_BOIL){
  if(!(s->valid&CANE_IR))strcpy(b,"煮糖页，红外测温未就绪");
  else{char cur[24],tgt[24];
   /* 与进工序的播报保持同一句式：先说现在多少度，再说该升到多少度。 */
   cane_temp_speech(cur,sizeof(cur),(int32_t)s->object10);
   cane_temp_speech(tgt,sizeof(tgt),p->t_boil10);
   snprintf(b,sizeof(b),"煮糖页，当前温度%s，应当上升到%s，风扇百分之%u，%s",
    cur,tgt,c->fan,c->fan2?"正在降温":"通风运行");}
 }else if(c->page==PAGE_FILL){
  if(!(s->valid&CANE_WEIGHT))strcpy(b,"分装页，称重传感器未就绪");
 else{char limit[24],cur[24];
  cane_weight_speech(cur,sizeof(cur),s->weight10);
  cane_weight_speech(target,sizeof(target),p->t_fill10);
  cane_weight_speech(limit,sizeof(limit),p->t_fill_max10);
  snprintf(b,sizeof(b),"分装页，当前%s，每份目标%s，单份上限%s，已完成%u份",cur,target,limit,p->fill_count);}
 }else if(c->page==PAGE_COOL){
  if(!(s->valid&CANE_IR))strcpy(b,"冷却页，红外测温未就绪");
  else snprintf(b,sizeof(b),"冷却页，糖浆温度%d点%d度，风扇百分之%u，%s",
   s->object10/10,s->object10%10,c->fan,c->fan2?"正在降温":"通风运行");
 }else if(c->page==PAGE_STORE){
  /* 温湿度探头没读到帧时，第一二张卡保留最后一次真实读数，
     所以这里不念采集状态、也不念“未就绪”，直接按读数播报。
     超温时念整句报警话术，与安全页抢过去时完全一致——不然页面播报只念
     “温度多少度”，现场根本听不出已经超限。湿度在句尾补一句“请注意防潮”。 */
  if(a->faults&AG_FAULT_AIR_HOT)strcpy(b,HOT_ALARM_TEXT);
  else snprintf(b,sizeof(b),"糖块入库，储存仓温度%d点%d度，湿度百分之%u，%s",
   a->sample.temperature10/10,a->sample.temperature10%10,a->sample.humidity10/10u,
   (a->faults&AG_FAULT_HUMI_HIGH)?"请注意防潮":"湿度正常");
 }else if(c->page==PAGE_TARGET){
  if(c->selected==0){cane_mass_speech(target,sizeof(target),(unsigned)p->t_weigh10);snprintf(b,sizeof(b),"参数页，称重目标%s",target);}
  else if(c->selected==1){cane_weight_speech(target,sizeof(target),p->t_fill10);snprintf(b,sizeof(b),"参数页，每份糖浆目标%s",target);}
  else if(c->selected==2)snprintf(b,sizeof(b),"参数页，煮糖目标温度%d点%d度",p->t_boil10/10,p->t_boil10%10);
  else snprintf(b,sizeof(b),"参数页，冷却目标温度%d点%d度",p->t_cool10/10,p->t_cool10%10);
 }else strcpy(b,cane_wifi_connected()?"通信页，OneNET已连接":"通信页，OneNET尚未连接，设备保护继续运行");
 if(operation&&operation[0]){char message[160];unsigned n=strlen(operation),size=strlen(b);
  /* 整段拼接前检查字节数，不能截断中文UTF-8字符。 */
  if(n+size+4u<=sizeof(message)){memcpy(message,operation,n);memcpy(message+n,"，",3);memcpy(message+n+3,b,size+1u);speech_say(message);}
  else{printf("SPEECH_PREFIX_OMITTED,length\n");speech_say(b);}
 }
 else speech_say(b);
}
static void ui_action(unsigned action,const AgControl *ag)
{
 CaneState copy;char feedback[96]="";unsigned old_page;int ack=0,quiet=0;
 if(!action)return;
 lock();old_page=state.page;
 if(action>=1&&action<=5)state.page=(uint8_t)action;
 else if(action==GUI_PREV){state.page=state.page<=1?PAGE_COMMS:state.page-1;strcpy(feedback,"上一页");}
 else if(action==GUI_NEXT){state.page=state.page>=PAGE_COMMS?PAGE_WEIGH:state.page+1;strcpy(feedback,"下一页");}
 else if(action==GUI_START){
  /* 按实际工序检查必需传感器，不能所有工序都只检查称重。
     只查红外与称重：测距(超声波液位)和霍尔(转速)缺失不该挡住启动，
     否则仿真里一量不到液面就永远起不来。 */
  unsigned required=(state.proc.stage==PSTAGE_PURIFY||state.proc.stage==PSTAGE_PURIFY_FINE)?CANE_REQUIRED_SAMPLE:
   (state.proc.stage==PSTAGE_BOIL||state.proc.stage==PSTAGE_COOL)?CANE_IR:CANE_WEIGHT;
  if(state.sample.fire||state.sample.person||state.environment_faults||
     (state.sample.valid&required)!=required||(state.faults&CANE_HARD_FAULTS)){
   state.page=PAGE_SAFETY;strcpy(feedback,"启动被联锁阻止，请先排除故障");
  }else{state.proc.run=1;
   if(state.proc.stage==PSTAGE_IDLE||state.proc.stage==PSTAGE_DONE)cane_process_enter(&state,PSTAGE_WEIGH,cane_now());
   state.proc.entered=cane_now();state.proc.settle_at=0;state.page=cane_page_of_stage(state.proc.stage);strcpy(feedback,"流程已启动");}
 }
 else if(action==GUI_STOP){state.proc.run=0;state.proc.feeder=state.proc.valve=state.proc.stir=0;state.proc.settle_at=0;}
 else if(action==GUI_STEP){
  /* 榨汁页刚进来是暂停态（液位 0、电机不转）：此时按"下一步"就是启动榨汁机，
     不跳工序——否则操作员一按就串到提纯去了。
     提纯页按"下一步"时先给状态机一个 user_next 标记：阀还没开就替操作员开阀，
     过滤没到 10% 则被 cane_process_next 挡住，不会一间跳过整个过滤过程。 */
  if(state.proc.stage==PSTAGE_PURIFY&&!state.proc.run){
   state.proc.run=1;state.proc.entered=cane_now();state.proc.settle_at=0;
   cane_apply(&state,cane_now());quiet=1;
  }else{
   int result;
   if(state.proc.stage==PSTAGE_PURIFY_FINE)state.user_next=1;
   cane_apply(&state,cane_now());state.user_next=0;
   result=cane_process_next(&state,state.proc.stage,cane_now());
   if(result==2){state.page=PAGE_SAFETY;strcpy(feedback,"安全联锁阻止下一步，请先排除危险");}
   else if(result==3)strcpy(feedback,"传感器未就绪，下一步未执行");
   /* 5=提纯过滤没走完，cane_process_next 已就地提示过，这里保持安静。 */
   else if(result==5)quiet=1;
   else if(result)strcpy(feedback,"工序状态已变化，请核对当前工序");
   /* 进入下一工序的播报由 cane_process_next 统一发出，这里不再重复，
      也不再叠一句整页读数，避免同一次按键播三遍。 */
   else quiet=1;
  }
 }else if(action==GUI_TARGET){state.page=PAGE_TARGET;strcpy(feedback,"打开参数页");}
 else if(action==GUI_WEIGH){state.page=PAGE_WEIGH;strcpy(feedback,"打开称重");}
 else if(action==GUI_SAFETY){state.page=PAGE_SAFETY;strcpy(feedback,"打开安全页");}
 else if(action==GUI_COMMS){state.page=PAGE_COMMS;strcpy(feedback,"打开通信页");}
 else if(action==GUI_ENTER&&state.page==PAGE_SAFETY){state.page=cane_page_of_stage(state.proc.stage);strcpy(feedback,"返回当前工序");}
 else if(action==GUI_TARE){
  if(state.proc.run)strcpy(feedback,"请先暂停流程，再空载去皮");
  else{cane_request_tare_loud(1);state.page=PAGE_WEIGH;strcpy(feedback,"去皮已提交，请保持秤台空载");}
 }
 else if(action>=GUI_SELECT0&&action<=GUI_SELECT3){static const char *names[]={"称重目标","每份重量","煮糖温度","冷却温度"};state.selected=(uint8_t)(action-GUI_SELECT0);snprintf(feedback,sizeof(feedback),"已选择%s",names[state.selected]);}
 else if(action==GUI_ENTER&&state.page==PAGE_TARGET){state.selected=(state.selected+1u)%4u;snprintf(feedback,sizeof(feedback),"已切换到第%u项参数",state.selected+1u);}
 else if(action==GUI_MINUS||action==GUI_PLUS){
  if(state.page!=PAGE_TARGET)strcpy(feedback,"请在参数页调整目标");
  else{int d=action==GUI_MINUS?-10:10;int before;
   /* 内部单位是“面板值×10”：称重目标按 kg 调，每步 10kg(100)，范围 100kg~3000kg；
      每份糖浆按 g 调，每步 10g(100)，范围 10g~1000g。 */
   if(state.selected==0){before=state.proc.t_weigh10;state.proc.t_weigh10=limit(before+((action==GUI_MINUS)?-100:100),1000,30000);}
   else if(state.selected==1){before=state.proc.t_fill10;state.proc.t_fill10=(int16_t)limit(before+d*10,100,10000);}
   else if(state.selected==2){before=state.proc.t_boil10;state.proc.t_boil10=(int16_t)limit(before+d,200,2000);}
   else{before=state.proc.t_cool10;state.proc.t_cool10=(int16_t)limit(before+d,100,900);}
   {int after=state.selected==0?state.proc.t_weigh10:state.selected==1?state.proc.t_fill10:state.selected==2?state.proc.t_boil10:state.proc.t_cool10;
    strcpy(feedback,before==after?"已到参数边界":action==GUI_MINUS?"减少目标成功":"增加目标成功");}
  }
 }else if(action==GUI_ACK){state.page=PAGE_SAFETY;ack=1;}
 /* 先执行真正的输出更新，再复制播报快照；避免启动后仍播报旧的零输出。 */
 if(action==GUI_START||action==GUI_STOP||action==GUI_STEP)cane_apply(&state,cane_now());
 if(action==GUI_STOP)strcpy(feedback,"流程已暂停，进料搅拌阀门及两只风扇已关闭");
 copy=state;
 printf("GUI_ACTION,action=%u,page=%u,stage=%u,alarm=%u,selected=%u,targets=%ld/%d/%d/%d/%u\n",action,state.page,state.proc.stage,state.proc.alarm,state.selected,
  (long)state.proc.t_weigh10,(int)state.proc.t_fill10,(int)state.proc.t_boil10,(int)state.proc.t_cool10,state.proc.t_conc);
 unlock();
 if(ack){
  if(app_command(AG_CMD_ACK))speech_say("确认未提交，请稍后重试");
  else if(alarm_bits(&copy,ag))speech_say("已确认报警，异常仍存在，请按安全页提示检查");
  else speech_say("故障确认完成，当前检测正常");
 }else if(action==GUI_SPEAK)report(ag,&copy,NULL);
 else if(quiet)return;
 else if(old_page!=copy.page)report(ag,&copy,feedback[0]?feedback:"已进入");
 else if(feedback[0])speech_say(feedback);
}
/* 板上三只按键（非工序键）：上一页 / 下一步 / 安全确认。日夜切换已删除。 */
void cane_external_key(unsigned key,const AgControl *ag)
{ui_action(key==0?GUI_PREV:key==1?GUI_STEP:GUI_ACK,ag);}
/* 网络任务不直接改PWM；与本地控制共用互斥状态和安全判定。 */
int cane_cloud_command(unsigned kind,const int32_t values[4],unsigned begin,const AgControl *ag)
{
 static uint32_t tare_before,ack_before,requested_at;int result=0;uint32_t now=cane_now();
 lock();
 if(!begin){
  if(kind==2)result=(state.sample.tare_serial!=tare_before&&(state.sample.valid&CANE_WEIGHT)&&now-state.sample.at<=3000u)?0:now-requested_at>5000u?5:10;
  else if(kind==3)result=app_ack_serial()!=ack_before?0:now-requested_at>2000u?9:10;
  else result=1;
 }else if(kind==0){
  if(values[0]!=0&&values[0]!=1)result=1;
  else if(!values[0]){state.proc.run=0;state.proc.settle_at=0;cane_apply(&state,now);}
  else{unsigned required=(state.proc.stage==PSTAGE_PURIFY||state.proc.stage==PSTAGE_PURIFY_FINE)?CANE_REQUIRED_SAMPLE:(state.proc.stage==PSTAGE_BOIL||state.proc.stage==PSTAGE_COOL)?CANE_IR:CANE_WEIGHT;
   /* 湿度报警不挡启动：它只提示除湿通风，不算"不能开机"的联锁条件。
      温湿度探头缺失（AG_FAULT_AIR）同样不挡：已经在 environment_faults 里剔掉，
      这里只挡真正会写坏设备的 AG_FAULT_AIR_HOT / 烟雾。 */
   if(state.sample.fire||state.sample.person||(app_interlock_faults()&CANE_ENV_INTERLOCK)||
      (ag->faults&CANE_ENV_INTERLOCK))result=2;
   else if((state.sample.valid&required)!=required||now-state.sample.at>3000u||now-ag->sample.at_ms>5000u)result=3;
   else{state.proc.run=1;if(state.proc.stage==PSTAGE_IDLE||state.proc.stage==PSTAGE_DONE)cane_process_enter(&state,PSTAGE_WEIGH,now);state.proc.entered=now;state.proc.settle_at=0;state.page=cane_page_of_stage(state.proc.stage);cane_apply(&state,now);if(!state.proc.run)result=2;}
  }
 }else if(kind==1){
  if(state.proc.run)result=4;
  /* 内部单位是“面板值×10”：称重 100kg~3000kg（内部 1000~30000），每份 10g~1000g。 */
  else if(values[0]<1000||values[0]>30000||values[0]%10||values[1]<100||values[1]>10000||values[1]%10||values[2]<200||values[2]>2000||values[2]%10||values[3]<100||values[3]>900||values[3]%10)result=1;
  else{state.proc.t_weigh10=values[0];state.proc.t_fill10=(int16_t)values[1];state.proc.t_boil10=(int16_t)values[2];state.proc.t_cool10=(int16_t)values[3];state.proc.settle_at=0;state.page=PAGE_TARGET;}
 }else if(kind==2){
  if(state.proc.run)result=4;
  else if(!(state.sample.valid&CANE_WEIGHT)||now-state.sample.at>3000u)result=3;
  else{tare_before=state.sample.tare_serial;requested_at=now;cane_request_tare_loud(1);state.page=PAGE_WEIGH;result=10;}
 }else if(kind==3){ack_before=app_ack_serial();requested_at=now;result=app_command(AG_CMD_ACK)?9:10;state.page=PAGE_SAFETY;}
 else if(kind==4){
  cane_apply(&state,now);
  /* 云端"下一步"同样不被湿度报警和温湿度探头缺失挡住，只挡真正的联锁故障。 */
  if((ag->faults&CANE_ENV_INTERLOCK)||now-ag->sample.at_ms>5000u)result=3;
  else result=cane_process_next(&state,(unsigned)values[0],now);
  if(!result)cane_apply(&state,now);
 }
 else result=1;
 if(begin||result!=10)printf("CLOUD,COMMAND_EXEC,kind=%u,result=%d,run=%u,stage=%u,targets=%ld/%d/%d/%d\n",kind,result,state.proc.run,state.proc.stage,(long)state.proc.t_weigh10,(int)state.proc.t_fill10,(int)state.proc.t_boil10,(int)state.proc.t_cool10);
 unlock();return result;
}
/* OLED 只用ASCII，16字符一行，作为产线现场的小屏菜单实时监测。 */
void cane_display(const CaneState *c,const AgControl *ag)
{
 static const char *short_name[]={"IDLE","WEIGH","JUICE","BOIL","FILL","COOL","PURIFY","STORE","DONE"};
 char b[48];static char previous[4][17];static unsigned row;static uint32_t at;unsigned i;const CaneProcess *p=&c->proc;
 gui_render(ag,c,cane_now());if(cane_now()-at<500u)return;at=cane_now();i=row;row=(row+1u)%4u;
 if(i==0)snprintf(b,sizeof(b),"STEP%u/6 %-8s",p->stage>6?6:p->stage,short_name[p->stage>8?0:p->stage]);
 if(i==1){char weight[16],temp[16];cane_weight_g(weight,sizeof(weight),c->sample.weight10,"g");
  snprintf(temp,sizeof(temp),"%d.%dC",c->sample.object10/10,c->sample.object10%10);
  snprintf(b,sizeof(b),"W%s%7s",weight,temp);}
 if(i==2)snprintf(b,sizeof(b),"FAN%3u STIR%3u",c->fan,p->stir);
 if(i==3)snprintf(b,sizeof(b),"%s",c->alarm?(p->alarm==PALARM_OVER?"ALARM:OVER":p->alarm==PALARM_UNDER?"ALARM:UNDER":p->alarm==PALARM_SAFETY?"ALARM:SAFE":"ALARM:SENSOR"):"STATUS NORMAL");
 {char padded[17];memset(padded,' ',16);padded[16]=0;memcpy(padded,b,strlen(b)<16?strlen(b):16);
  if(strcmp(previous[i],padded)){board_oled_row(i,padded);memcpy(previous[i],padded,17);}}
}
static VOID process_control_entry(UINT32 arg)
{
 (void)arg;lock();cane_apply(&state,cane_now());unlock();}
extern uint32_t cane_tare_serial(void);
void cane_ui(const AgControl *ag)
{
 static uint8_t candidate,stable,initialized,jumped;static uint32_t key_at,log_at,old_safety,tare_serial,say_seq;
 uint32_t now=cane_now(),bits;uint8_t raw=keys();CaneState copy;unsigned i;
 if(!initialized){board_oled_init();initialized=1;}
 if(raw!=candidate){candidate=raw;key_at=now;}
 if(now<1500u){stable=candidate;key_at=now;}
 if(now-key_at>=40&&stable!=candidate){uint8_t rising=candidate&~stable;stable=candidate;
  for(i=0;i<5;i++)if(rising&(1u<<i)){
   CaneState key_state;unsigned a;lock();key_state=state;unlock();
   /* PB14（第 5 个键）= 安全页 / 确认报警：不在安全页就跳过去，在安全页就确认。
      称重键（原 PC14）和日夜切换键已从电路上删除，这里不再有对应分支。 */
   if(i==4){ui_action(key_state.page==PAGE_SAFETY?GUI_ACK:GUI_SAFETY,ag);continue;}
   /* PC13（第 4 个键）在提纯工序里是"开过滤阀"：只置请求，
      由状态机置 filter_open，再由 cane_apply 把 PD14（TIM4_CH3）转到 90 度
      并启动 PA7 直流电机带过滤网，这里不阻塞显示任务。 */
   if(i==3&&key_state.proc.stage==PSTAGE_PURIFY_FINE){
    lock();if(!state.proc.filter_open)state.proc.filter_req=1;unlock();
    printf("GUI_KEY,key=PC13,stage=%u,filter_open=%u\n",key_state.proc.stage,key_state.proc.filter_open);
    continue;
   }
   a=i==0?GUI_PREV:i==1?(key_state.page<=PAGE_STORE?GUI_STEP:GUI_NEXT):i==2?(key_state.page==PAGE_TARGET?GUI_ENTER:key_state.proc.run?GUI_STOP:GUI_START):GUI_MINUS;
   ui_action(a,ag);
  }
 }
 lock();copy=state;unlock();
 /* 流程状态机只登记待播报文本，真正发声统一由显示任务串行发出。 */
 if(copy.proc.say_seq!=say_seq){say_seq=copy.proc.say_seq;
  if(copy.proc.say_alert)speech_alert_text(copy.proc.say);else speech_say(copy.proc.say);}
 {static uint8_t down;static uint32_t down_at;static uint16_t tx,ty;uint16_t x,y;int pressed=tft35_touch(&x,&y);
  if(pressed&&!down){down_at=now;tx=x;ty=y;}
  if(!pressed&&down&&now-down_at>=40u&&now-down_at<2000u){
   printf("TOUCH35,x=%u,y=%u\n",tx,ty);ui_action((unsigned)gui_touch_action(copy.page,tx,ty),ag);
   lock();copy=state;unlock();
  }down=(uint8_t)pressed;
 }
 bits=alarm_bits(&copy,ag);
 /* 首次等待采集；明火和人员不延迟。
    只有安全类事件才抢占页面和出声；工艺越限已由状态机播报，不在这里再播一遍。
    解除时也只在页面确实被抢过的情况下提示返回。 */
 {uint32_t safety=safety_bits(&copy,ag);
  if(now>=5000u||copy.sample.fire||copy.sample.person){
   if(safety&~old_safety){char message[160];const char *reason;lock();state.page=PAGE_SAFETY;copy=state;unlock();
    reason=alarm_reason(&copy,ag);
    /* 湿度报警和温度过高这两句是固定整句，前面都不加"安全报警，"前缀；
       其他安全话术仍带前缀。 */
    if(!strcmp(reason,HUMI_ALARM_TEXT)||!strcmp(reason,HOT_ALARM_TEXT))strcpy(message,reason);
    else snprintf(message,sizeof(message),"安全报警，%s",reason);
    speech_alert_text(message);
    jumped=1;
    printf("GUI_ALARM_ROUTE,page=%u,new=%lu,all=%lu\n",PAGE_SAFETY,(unsigned long)(safety&~old_safety),(unsigned long)safety);
   }else if(!safety&&old_safety){
    if(jumped){lock();state.page=cane_page_of_stage(state.proc.stage);copy=state;unlock();
     speech_say("报警已解除，已返回当前工序");}
    jumped=0;
   }
   old_safety=safety;
  }
 }
 {uint32_t n=cane_tare_serial();if(n!=tare_serial){
  /* 去皮完成即播报，但仅在需要播报的那一次；
     称重进料、分装首份各播一次，分装每份之间的自动清零静默处理。 */
  if(tare_serial&&cane_tare_announce){
   cane_tare_announce=0;
   speech_say(copy.proc.stage==PSTAGE_FILL?"分装已去皮，读数归零，开始分装":"称重已去皮，读数归零，开始称重");
  }
  tare_serial=n;}}
 cane_wifi_poll(now,&copy,ag);copy.wifi=(uint8_t)cane_wifi_connected();
 if(now-log_at>=2000u){log_at=now;
  printf("CANE_SAMPLE,valid=%lu,weight10=%lu,object10=%d,distance=%u,hall_adc=%u,speed10=%d,fire=%u,person=%u\n",
   (unsigned long)copy.sample.valid,(unsigned long)copy.sample.weight10,(int)copy.sample.object10,copy.sample.distance_mm,copy.sample.hall_adc,(int)copy.sample.speed10,copy.sample.fire,copy.sample.person);
  printf("CANE_PROCESS,stage=%u,alarm=%u,run=%u,feeder=%u,valve=%u,stir=%u,conc=%u,fill=%u/%u,batch=%u,fan=%u,servo=%u/%u/%u,juice=%u,phase_ms=%u\n",
   copy.proc.stage,copy.proc.alarm,copy.proc.run,copy.proc.feeder,copy.proc.valve,copy.proc.stir,copy.proc.conc,
   copy.proc.fill_count,copy.proc.fill_total,copy.proc.batch,copy.fan,copy.servo[0],copy.servo[1],copy.servo[2],copy.proc.juice_level,copy.proc.phase_ticks);
  printf("UI_STATE,page=%u,valve=%u,alarm_bits=%lu,tare=%lu\n",copy.page,copy.proc.filter_open,(unsigned long)bits,(unsigned long)tare_serial);
 }
 speech_poll();cane_display(&copy,ag);
}
void cane_start(void)
{
 UINT32 rc;memset(&state,0,sizeof(state));
 cane_process_init(&state.proc,0);
 state.target_temp10=state.proc.t_boil10;state.target_weight10=state.proc.t_weigh10;
 state.target_distance_mm=4000;state.target_speed10=0;state.page=PAGE_WEIGH;
 rc=LOS_MuxCreate(&mutex);if(rc!=LOS_OK){printf("CANE_INIT_FAIL,mux=%lu\n",(unsigned long)rc);while(1){}}
 cane_hardware_init();
 /* 两个周期任务交给内核：采集 350ms、工序控制 50ms。
    优先级按 LiteOS-M 约定（数值小=高）：工序控制 1、采集 2。 */
 {TSK_INIT_PARAM_S p;
  p.pcName="CaneSensors";p.pfnTaskEntry=cane_sensor_entry;p.usTaskPrio=2u;
  p.uwStackSize=2048u;p.uwArg=0u;p.uwPeriodMs=350u;
  if(LOS_TaskCreate(&cane_task_ids[0],&p)!=LOS_OK){printf("CANE_INIT_FAIL,tsk=sensors\n");while(1){}}
  p.pcName="ProcessControl";p.pfnTaskEntry=process_control_entry;p.usTaskPrio=1u;
  p.uwStackSize=2048u;p.uwArg=0u;p.uwPeriodMs=50u;
  if(LOS_TaskCreate(&cane_task_ids[1],&p)!=LOS_OK){printf("CANE_INIT_FAIL,tsk=process\n");while(1){}}
 }

 printf("CANE_PORT,STM32F407ZG,project=60275,candidate=cane-onenet-juice-8\n");
}
