#include "app_runtime.h"
#include "cane.h"
#include "board_port.h"
#include "los_task.h"
#include "los_queue.h"
#include "los_mux.h"
#include "los_memory.h"
#include "board_hal.h"
#include "speech.h"
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t at;uint8_t command;} AppCommand;
typedef struct {uint32_t at,faults;uint8_t fan,lamp,smoke,level;} AppEvent;
static UINT32 sample_q,command_q,event_q,snapshot_mux,task_ids[4];
static AgControl snapshot;
static volatile uint32_t sample_drops,command_drops,event_drops,expired_commands,interlock;
static volatile uint32_t process_vent;
static volatile uint32_t ack_serial;
uint32_t app_ack_serial(void){return ack_serial;}
uint32_t app_interlock_faults(void){return interlock;}
void app_set_process_vent(unsigned enabled){process_vent=enabled?1u:0u;}
static uint32_t now_ms(void){return board_millis();}
static void fail(const char *where,UINT32 rc)
{board_all_off();printf("FATAL,%s,0x%lx\n",where,(unsigned long)rc);while(1){}}
static void check(const char *where,UINT32 rc){if(rc!=LOS_OK)fail(where,rc);}

/* 四个任务写成 LiteOS-M 的 TSK_ENTRY_FUNC 形态：入口收一个 UINT32 arg，跑完就返回。
   节拍由内核（LOS_TaskCreate 的 uwPeriodMs）负责，函数里不再自己睡，
   所以采集耗时不会把 250ms 拖成 280ms。 */
static VOID sensor_entry(UINT32 arg)
{
 (void)arg;
 AgSample s;board_read_sample(&s,now_ms());
 if(LOS_QueueWriteCopy(sample_q,&s,sizeof(s),LOS_NO_WAIT)!=LOS_OK){
  AgSample old;UINT32 size=sizeof(old);(void)LOS_QueueReadCopy(sample_q,&old,&size,LOS_NO_WAIT);sample_drops++;
  if(LOS_QueueWriteCopy(sample_q,&s,sizeof(s),LOS_NO_WAIT)!=LOS_OK)sample_drops++;
 }
}
static AgControl control_state,control_previous;
static VOID control_entry(UINT32 arg)
{
 (void)arg;
 AgSample s;AppCommand cmd;UINT32 size=sizeof(s);uint32_t now;
 while(LOS_QueueReadCopy(sample_q,&s,&size,LOS_NO_WAIT)==LOS_OK){ag_accept(&control_state,&s);size=sizeof(s);}
 now=now_ms();size=sizeof(cmd);
 while(LOS_QueueReadCopy(command_q,&cmd,&size,LOS_NO_WAIT)==LOS_OK){
  if(now-cmd.at<=500u){ag_command(&control_state,cmd.command,now);if(cmd.command==AG_CMD_ACK)ack_serial++;}else expired_commands++;
  size=sizeof(cmd);
 }
 ag_step(&control_state,now);
 /* 风扇转速由 cane_apply 独占写入（cane_outputs 里），这里只管环境侧的通风许可。 */
 board_apply(&control_state);interlock=control_state.faults;
 if(control_state.faults!=control_previous.faults||control_state.fan!=control_previous.fan||control_state.lamp!=control_previous.lamp){
  AppEvent e={now,control_state.faults,control_state.fan,control_state.lamp,control_state.sample.smoke,control_state.sample.level};
  if(LOS_QueueWriteCopy(event_q,&e,sizeof(e),LOS_NO_WAIT)!=LOS_OK)event_drops++;
  control_previous=control_state;
 }
 check("control_lock",LOS_MuxPend(snapshot_mux,LOS_WAIT_FOREVER));snapshot=control_state;
 check("control_unlock",LOS_MuxPost(snapshot_mux));
}
int app_command(uint8_t command)
{AppCommand c={now_ms(),command};if(LOS_QueueWriteCopy(command_q,&c,sizeof(c),LOS_NO_WAIT)!=LOS_OK){command_drops++;return -1;}return 0;}
static VOID ui_entry(UINT32 arg)
{
 (void)arg;
 static uint8_t candidate=0,stable=0;static uint32_t changed_at=0,render_at=0,resource_at=0;
 static AgControl copy;uint32_t now=now_ms();uint8_t raw=board_buttons();unsigned i;AppEvent e;UINT32 size=sizeof(e);
 if(raw!=candidate){candidate=raw;changed_at=now;}
 if(now<1500u){stable=candidate;changed_at=now;}
 check("ui_lock",LOS_MuxPend(snapshot_mux,LOS_WAIT_FOREVER));copy=snapshot;
 check("ui_unlock",LOS_MuxPost(snapshot_mux));
 if(now-changed_at>=40u&&stable!=candidate){uint8_t rising=candidate&~stable;stable=candidate;
  for(i=0;i<3;i++)if(rising&(1u<<i))cane_external_key(i,&copy);
 }
 while(LOS_QueueReadCopy(event_q,&e,&size,LOS_NO_WAIT)==LOS_OK){
  printf("ENV_EVENT,%lu,faults=%lu,fan=%u,lamp=%u,smoke=%u,level=%u\n",(unsigned long)e.at,(unsigned long)e.faults,e.fan,e.lamp,e.smoke,e.level);size=sizeof(e);
 }
 if(now-render_at>=AG_RENDER_PERIOD_MS){render_at=now;
  printf("STATUS,%lu,valid=%u,drop=%lu/%lu/%lu,expired_cmd=%lu,heap_used=%lu,distance_mm=%u,smoke_adc=%u,light_adc=%u,smoke_ppm=%u,smoke=%u,level=%u,faults=%lu,fan=%u,lamp=%u,air_age=%lu\n",
   (unsigned long)now,copy.sample.valid,(unsigned long)sample_drops,(unsigned long)command_drops,(unsigned long)event_drops,(unsigned long)expired_commands,
   (unsigned long)LOS_MemTotalUsedGet(m_aucSysMem0),copy.sample.distance_mm,copy.sample.smoke_adc,copy.sample.light_adc,copy.sample.smoke_ppm,copy.sample.smoke,copy.sample.level,(unsigned long)copy.faults,copy.fan,copy.lamp,(unsigned long)(now-copy.sample.air_at_ms));
 }
 cane_ui(&copy);
 if(now-resource_at>=5000u){resource_at=now;cane_resource_report(now);}
}
/* 过滤阀/液体出料闸舵机接在 PD14（TIM4_CH3），由 cane_outputs 写 CCR3，
   定时器硬件自己持续输出 50Hz 信号，不再需要单独的"阀控任务"反复发脉冲。
   旧实现把舵机帧打在 PD4 上，而电路里 PD4 端子没有对端，等于空转。 */
/* 任务创建：写成 LiteOS-M 的 TSK_INIT_PARAM_S 填表 + LOS_TaskCreate 的标准两步。
   注意优先级方向——LiteOS-M 里**数值小=优先级高**，和 CMSIS 的 osPriority 相反，
   所以原来的 4/6/10 要换成 4/6/10 在 LiteOS-M 下反而变成"显示任务最低"，
   这里按"控制最高、采集次之、显示最低"重新给：2 / 3 / 4。 */
static VOID task(unsigned slot,const CHAR *name,TSK_ENTRY_FUNC entry,UINT16 priority,UINT32 period)
{
 TSK_INIT_PARAM_S p;
 p.pcName=(CHAR *)name;p.pfnTaskEntry=entry;p.usTaskPrio=priority;
 p.uwStackSize=2048u;p.uwArg=0u;p.uwPeriodMs=period;
 check(name,LOS_TaskCreate(&task_ids[slot],&p));
}
VOID app_start(VOID)
{
 board_sensor_init();
 board_display_init();
 speech_init();
 check("sample_queue",LOS_QueueCreate("samples",4,&sample_q,0,sizeof(AgSample)));
 check("command_queue",LOS_QueueCreate("commands",8,&command_q,0,sizeof(AppCommand)));
 check("event_queue",LOS_QueueCreate("events",16,&event_q,0,sizeof(AppEvent)));
 check("snapshot_mutex",LOS_MuxCreate(&snapshot_mux));
 ag_init(&snapshot,now_ms());ag_init(&control_state,now_ms());control_previous=control_state;
 cane_start();
 task(0,"Control",control_entry,2u,AG_CONTROL_PERIOD_MS);
 task(1,"Sensors",sensor_entry,3u,AG_SAMPLE_PERIOD_MS);
 task(2,"Display",ui_entry,4u,AG_UI_PERIOD_MS);
}
