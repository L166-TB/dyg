#ifndef AGRI_CONTROL_H
#define AGRI_CONTROL_H
#include <stdint.h>
/* 蔗糖厂（生产车间）环境监测。
   MQ-2 组件参数 smog 的量程就是 MQ-2 的 0~10000ppm：平台实测参数192（洁净空气）
   对应 ADC253，参数10000 对应 ADC869，两点线性插值即可把 ADC 还原成 ppm。
   报警线 500ppm：达到或超过 500ppm 报警，一旦低于 500ppm 立即解除（不留滞回，
   现场要求"烟散了报警就撤"，屏幕和蜂鸣器要跟着浓度一起走）。 */
#define AG_SMOKE_ADC_BASE 253u
#define AG_SMOKE_ADC_FULL 869u
#define AG_SMOKE_PPM_BASE 192u
#define AG_SMOKE_PPM_FULL 10000u
#define AG_SMOKE_PPM_ALARM 500u
#define AG_SMOKE_PPM_CLEAR 500u
/* 储存仓（DHT11_1，PA0）湿度过高报警：上限 70%，
   到 70% 报警、回落到 68% 以下才解除。留 2 个点的滞回，
   免得湿度在 70% 上下抖一下屏幕、蜂鸣器和语音就来回跳。 */
#define AG_HUMI_ALARM 700u
#define AG_HUMI_CLEAR 680u
/* 温度过高报警线：DHT11_1 读到 50℃ 及以上报警，回落到 47℃ 以下才解除。
   DHT11 本身量程 0~50℃，50℃ 就是它读得到的最高一档，所以这条线只能定在
   量程顶端——以前定 40℃ 时，操作员在平台上把参数拖到 45 度以上就一路踩着
   报警线走，屏幕、蜂鸣器、语音全在响，看不出"到底多高才叫高"。 */
#define AG_HOT_ALARM 500
#define AG_HOT_CLEAR 470
/* 上电宽限期：这段时间内的"没有采集数据"一律不算故障。
   仿真器刚点运行时，DHT11/红外/称重都还没吐出第一帧，而上位机操作员
   可能正对着屏幕讲解，几分钟都不点按钮。以前这种情况会被判成
   "采集未就绪"直接跳安全页并出声，把介绍录制打断。
   现在前 AG_ENV_GRACE_MS 内只显示"正在采集"，不走报警、不抢页面。 */
#define AG_ENV_GRACE_MS 5000u
/* 采集饥饿超时：控制任务每 50ms 跑一拍，而环境数据每 250ms 才来一条，
   所以连续 1.5 秒一条采样都没送进控制任务，才是真的采集任务卡死。 */
#define AG_SAMPLE_STARVE_MS 1500u
#define AG_AIR 1u
#define AG_SMOKE 2u
#define AG_LIGHT 4u
#define AG_LEVEL 8u
#define AG_ALL 15u
enum {AG_CMD_NONE,AG_CMD_ACK};
enum {AG_FAULT_STALE=1u,AG_FAULT_SMOKE_SENSOR=2u,AG_FAULT_AIR=4u,
 AG_FAULT_LIGHT=8u,AG_FAULT_LEVEL=16u,AG_FAULT_SMOKE_HIGH=32u,
 AG_FAULT_LEVEL_HIGH=64u,AG_FAULT_AIR_HOT=128u,AG_FAULT_TIMING=256u,
 AG_FAULT_HUMI_HIGH=512u};
typedef struct {
 uint32_t at_ms,air_at_ms;
 int16_t temperature10;
 uint16_t humidity10;
 uint8_t smoke,light,level,valid;
 uint16_t smoke_ppm;
 uint16_t distance_mm,smoke_adc,light_adc;
} AgSample;
typedef struct {
 AgSample sample;
 uint32_t faults,sticky_faults,last_step,started,last_accept;
 uint8_t fan,lamp,ever_sample,filtered,grace_done;
 uint8_t smoke_alarm,level_alarm,hot_alarm,humi_alarm;
} AgControl;
void ag_init(AgControl *c,uint32_t now);
void ag_accept(AgControl *c,const AgSample *s);
void ag_command(AgControl *c,uint8_t command,uint32_t now);
void ag_step(AgControl *c,uint32_t now);
int ag_normalize(uint16_t raw,uint16_t low,uint16_t high);
unsigned ag_smoke_ppm(uint16_t adc);
int ag_decode_dht(const uint8_t frame[5],int16_t *temperature10,uint16_t *humidity10);
#endif
