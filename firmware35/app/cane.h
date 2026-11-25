#ifndef CANE_H
#define CANE_H
#include <stdint.h>
#include "board_hal.h"
#include "agri_control.h"
enum {CANE_IR=1u,CANE_WEIGHT=2u,CANE_DISTANCE=4u,CANE_HALL=8u};
/* 真正会挡住流程推进的设备故障位（指的是 CaneState.faults，不是环境故障）。
     bit0 红外测温、bit1 称重、bit4 设备反馈超时。
     注意 bit2(测距) / bit3(霍尔) 只用来提示，不挡流程：超声波液位在仿真里
     经常量不到，霍尔转速只是显示用；以前把整组 valid 都当门槛，
     结果"测距没就绪"就把流程和电机一起停住了。
     温湿度探头缺失（agri_control 的 AG_FAULT_AIR）根本不在这个位域里，
     由 cane_apply 单独剔出联锁，只报警。 */
#define CANE_HARD_FAULTS (CANE_IR|CANE_WEIGHT|16u)
/* 启动/推进必备采样：只要求红外和称重，测距与霍尔不参与门槛判定。 */
#define CANE_REQUIRED_SAMPLE (CANE_IR|CANE_WEIGHT)
/* 环境侧真正会挡启动/推进的故障位：烟雾超标、环境超温、采集卡死。
    湿度过高(AG_FAULT_HUMI_HIGH)和温湿度探头未读到帧(AG_FAULT_AIR)都只报警，
    不参与联锁——探头读不到不等于产线不能跑，仿真里 DHT11 常常吐不出完整帧。 */
#define CANE_ENV_INTERLOCK (AG_FAULT_STALE|AG_FAULT_SMOKE_SENSOR|AG_FAULT_SMOKE_HIGH|AG_FAULT_AIR_HOT|AG_FAULT_LEVEL_HIGH|AG_FAULT_TIMING)
typedef struct {
 uint32_t at,valid,raw_weight,tare_serial;
 int16_t object10,speed10;
 /* 称重按 0.1g 计数，500kg 需要 5000000，16 位装不下，统一用 32 位。 */
 uint32_t weight10;
 uint16_t distance_mm,hall_adc;
 uint8_t fire,person;
} CaneSample;
/* 六道工序：1称重 2榨汁 6提纯子步骤 3煮糖 4分装 5冷却 7糖块入库 */
enum {PSTAGE_IDLE=0,PSTAGE_WEIGH,PSTAGE_PURIFY,PSTAGE_BOIL,PSTAGE_FILL,PSTAGE_COOL,PSTAGE_PURIFY_FINE,PSTAGE_STORE,PSTAGE_DONE};
/* 报警原因按工序各自判定，界面上直接显示对应中文。 */
enum {PALARM_NONE=0,PALARM_OVER,PALARM_UNDER,PALARM_SENSOR,PALARM_TIMEOUT,PALARM_SAFETY};
/* 榨汁液位上限与提纯完成值：状态机和界面共用同一份，避免两边写死不同数字。 */
#define CANE_JUICE_FULL_CM 180u
#define CANE_CONC_CLEAN    10u
typedef struct {
 uint8_t juice_level;
 uint16_t phase_ticks;
 uint32_t progress_at;
 uint8_t stage;      /* PSTAGE_* 当前工序 */
 uint8_t run;        /* 1=工序运行；0=工序动作和两只风扇关闭且不推进 */
 uint8_t alarm;      /* PALARM_* 当前工序报警 */
 uint8_t spoken;     /* 已播报的报警与工序，避免重复喊 */
 uint8_t feeder;     /* 入料闸门使能 */
 uint8_t valve;      /* 当前工序出液阀 1=开 0=关 */
 uint8_t fill_count; /* 已分装份数 */
 uint8_t fill_total; /* 本批分装份数 */
 uint8_t batch;      /* 已完成批次数 */
 uint8_t conc;       /* 杂质浓度百分数，提纯过程中由高向低走 */
 uint8_t conc_from;  /* 本轮提纯起始杂质浓度，用于线性下降 */
 uint8_t purify_done;/* 本轮提纯已播报过达标，避免重复喊 */
 uint8_t juice_done; /* 榨汁完成已播报过，避免每周期重复喊 */
 uint8_t cool_said;  /* 冷却段已播报过风扇降温 */
 uint8_t cool_fan;   /* 冷却风扇当前状态 1=开(温度高于45度) 0=关(0~45度) */
 uint8_t boil_fan;   /* 煮糖风扇当前状态 1=开(温度高于105度) 0=关 */
 uint8_t stir;       /* 搅拌电机转速百分数 0~100 */
 int32_t t_weigh10;  /* 称重目标 0.1g，500kg=5000000 超出 int16 范围，必须 32 位 */
 int16_t t_fill10;   /* 每份糖浆目标 0.1g */
 int16_t t_fill_max10; /* 分装单份上限 0.1g，超过即报警关阀 */
 int16_t t_boil10;   /* 煮糖目标温度 0.1度 */
 int16_t t_cool10;   /* 冷却目标温度 0.1度 */
 uint8_t t_conc;     /* 杂质浓度上限百分数 */
 uint32_t entered;   /* 进入本工序的时刻 */
 uint32_t settle_at; /* 达标稳定计时起点，0=未计时 */
 uint32_t tare_serial; /* 分装去皮请求前的序号，必须等待真实采样确认 */
 uint8_t tare_wait;  /* 等待新去皮样本，期间阀门保持关闭 */
 int32_t fill_last10;/* 分装上周期读数，用来判断数值是否还在变化（0.1g） */
 uint32_t fill_start_at; /* 分装首次拿到有效读数的时刻，之后先等 2 秒再判稳定 */
 uint8_t fill_done;  /* 本份已按稳定值判定并播报过，避免重复喊 */
 uint8_t say_seq;    /* 播报序号，显示任务比对后真正发声 */
 uint8_t say_alert;  /* 1=报警播报，抢占普通导航 */
 /* 榨汁液位按厘米走 0~180；提纯阀门由 PC13 按键开门、PD14 步进电机转 90 度。 */
 uint8_t juice_cm;   /* 甘蔗汁液位 cm，0~180 */
 uint8_t weigh_said; /* 称重达标已播报过 */
 uint8_t filter_req; /* PC13 按键请求开过滤阀 */
 uint8_t filter_open;/* 过滤阀已开（步进电机已转 90 度） */
 uint8_t filter_done;/* 浑浊度已降到 10% 并播报过 */
 uint8_t boil_say;   /* 煮糖达标（102 度）提示已播报 */
 uint8_t boil_intro; /* 煮糖进工序的"当前温度/应当上升到"已在流程内播报 */
 uint8_t store_say;  /* 入库进工序的检测提示已播报 */
 char say[128];      /* 待播报文本 */
} CaneProcess;
typedef struct {
 CaneSample sample;
 CaneProcess proc;
 uint32_t target_weight10;uint16_t target_distance_mm;
 int16_t target_temp10,target_speed10;
 /* servo[0]=PD13 称重加料阀，servo[1]=PD12 入料闸，
    servo[2]=PD14 液体出料闸，servo[3]=PD14 提纯过滤阀（步进电机 0/90 度）。
    2、3 两路物理上都落在 TIM4_CH3，由 cane_hal 做仲裁，同一时刻只会有一路生效。 */
 uint16_t servo[4],fan,fan2,motor;
 uint32_t faults,environment_faults;
 /* night 已删除：日夜切换从电路和界面上一并去掉，只剩一套白底配色。 */
 uint8_t page,selected,alarm,wifi,feeder;
 /* 操作员"按下一步"的一次性标记：提纯段用它把"开过滤阀"和"跳工序"合并成一次按键。 */
 uint8_t user_next;
} CaneState;
uint32_t cane_now(void);
void cane_hardware_init(void);
void cane_request_tare(void);
/* 去皮同时指定是否需要语音播报；分装每份之间的自动清零走静默。 */
void cane_request_tare_loud(unsigned say);
extern volatile uint8_t cane_tare_announce;
uint32_t cane_tare_serial(void);
void cane_outputs(const CaneState *c,uint32_t now);
/* 舵机（PD14 = TIM4_CH3）：标准 50Hz 舵机信号，0~180 度对应 0.5~2.5ms 高电平。
   电路上 servo_3「液体出料闸门」的 PWM 经 Tunnel_74(PinId=PD14) 接到 PD14，
   所以这只舵机既是液体出料闸、也是提纯过滤阀；角度写进 CCR3 后由定时器保持。 */
#define CANE_VALVE_CLOSE_DEG 0u
#define CANE_VALVE_OPEN_DEG  90u
void cane_servo_frame(unsigned angle);
/* 过滤阀目标角度：已开=90 度，未开=0 度（保留给需要单独查询的场合）。 */
unsigned cane_filter_valve_angle(void);
void cane_feeder_step(void);
void cane_read(CaneSample *s);
void cane_process_init(CaneProcess *p,uint32_t now);
void cane_process_step(CaneState *c,uint32_t now);
int cane_process_next(CaneState *c,unsigned expected,uint32_t now);
void cane_process_enter(CaneState *c,unsigned stage,uint32_t now);
unsigned cane_concentration(uint16_t distance_mm);
/* 工序 -> 界面页：提纯与榨汁共用 P2，入库独占 P6。 */
uint8_t cane_page_of_stage(unsigned stage);
const char *cane_stage_name(unsigned stage);
const char *cane_alarm_text(unsigned stage,unsigned alarm);
void cane_apply(CaneState *c,uint32_t now);
void cane_display(const CaneState *c,const AgControl *ag);
void cane_ui(const AgControl *ag);
void cane_external_key(unsigned key,const AgControl *ag);
void cane_start(void);
void cane_resource_report(uint32_t now);
void cane_wifi_poll(uint32_t now,const CaneState *c,const AgControl *ag);
const char *cane_cloud_status(void);
/* 云命令只经业务层执行；10表示等待真实采样或调度确认。 */
int cane_cloud_command(unsigned kind,const int32_t values[4],unsigned begin,const AgControl *ag);
void board_text_row(uint16_t y,const char *text,uint16_t color);
#endif
