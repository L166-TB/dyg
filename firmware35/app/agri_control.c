#include "agri_control.h"
#include <string.h>
static int clamp100(int v){return v<0?0:v>100?100:v;}
int ag_normalize(uint16_t raw,uint16_t low,uint16_t high)
{int d=(int)high-low;return d?clamp100(((int)raw-low)*100/d):-1;}
/* ADC -> 烟雾浓度 ppm：以平台实测的两点（253=192ppm、869=10000ppm）线性插值。 */
unsigned ag_smoke_ppm(uint16_t adc)
{
 int32_t span=(int32_t)AG_SMOKE_ADC_FULL-(int32_t)AG_SMOKE_ADC_BASE;
 int32_t v=(int32_t)adc-(int32_t)AG_SMOKE_ADC_BASE;
 int32_t ppm;
 if(span<=0)return AG_SMOKE_PPM_BASE;
 if(v<=0)return AG_SMOKE_PPM_BASE;          /* 低于洁净空气底噪，按本底算 */
 ppm=(int32_t)AG_SMOKE_PPM_BASE+
     v*((int32_t)AG_SMOKE_PPM_FULL-(int32_t)AG_SMOKE_PPM_BASE)/span;
 if(ppm<(int32_t)AG_SMOKE_PPM_BASE)ppm=(int32_t)AG_SMOKE_PPM_BASE;
 if(ppm>12000)ppm=12000;                    /* 超量程按 12000 封顶 */
 return (unsigned)ppm;
}
int ag_decode_dht(const uint8_t f[5],int16_t *t,uint16_t *h)
{
 int temperature;uint16_t humidity;
 if(!f||!t||!h||(uint8_t)(f[0]+f[1]+f[2]+f[3])!=f[4])return -1;
 temperature=f[2]*10+(f[3]&0x7f);if(f[3]&0x80)temperature=-temperature;
 humidity=(uint16_t)(f[0]*10u+f[1]);
 /* 量程上限：DHT11 官方是 0~50℃、20~90%RH。
     温度封顶取 550（55℃）而不是 500，就是为了让"刚好 50℃"这一档能作为
     报警线：如果封在 500，拖到 50 度只是踩在边界上，擦边一次就丢一帧。 */
 if(temperature<0||temperature>550||humidity>1000)return -1;
 *t=(int16_t)temperature;*h=humidity;return 0;
}
void ag_init(AgControl *c,uint32_t now)
{memset(c,0,sizeof(*c));c->last_step=now;c->started=now;c->last_accept=now;}
void ag_accept(AgControl *c,const AgSample *sample)
{
 AgSample s=*sample;
 if(c->ever_sample&&(int32_t)(s.at_ms-c->sample.at_ms)<0)return;
 if(s.temperature10<0||s.temperature10>550||s.humidity10>1000)s.valid&=(uint8_t)~AG_AIR;
 if(s.smoke>100)s.valid&=(uint8_t)~AG_SMOKE;
 if(s.light>100)s.valid&=(uint8_t)~AG_LIGHT;
 if(s.level>100)s.valid&=(uint8_t)~AG_LEVEL;
 /* 烟雾不平滑，避免滤波延后报警；照度平滑用于照明滞回。 */
 if(c->filtered&&(s.valid&c->sample.valid&AG_LIGHT))s.light=(uint8_t)((c->sample.light+3u*s.light+2u)/4u);
 c->sample=s;c->ever_sample=c->filtered=1;c->last_accept=s.at_ms;
}
void ag_command(AgControl *c,uint8_t command,uint32_t now)
{(void)now;if(command==AG_CMD_ACK)c->sticky_faults=0;}
void ag_step(AgControl *c,uint32_t now)
{
 int grace=!c->grace_done;
 int fresh;
 /* 上电宽限期：从 ag_init 起算 AG_ENV_GRACE_MS 内不判任何采集类故障。
    这里必须放在最前面，且不再用 dt>250 判调度超时——控制任务本来
    就每 50ms 跑，而采样 250ms 才来一条，那个判据从上电起每拍都在误报。 */
 if(now-c->started<AG_ENV_GRACE_MS)grace=1;else c->grace_done=1;
 /* last_step 只做心跳记录，不再参与故障判定，所以也不需要局部 dt。 */
 c->last_step=now;
 /* 真·卡死判据：连续 AG_SAMPLE_STARVE_MS 一条采样都没送进来。
     宽限期内同样豁免，避免仿真器首帧慢就跳安全页。 */
 if(!grace&&now-c->last_accept>AG_SAMPLE_STARVE_MS)c->sticky_faults|=AG_FAULT_TIMING;
 fresh=c->ever_sample&&now-c->sample.at_ms<=5000u;
 c->faults=c->sticky_faults;
 if(!grace){
  if(!fresh)c->faults|=AG_FAULT_STALE;
  if(!fresh||!(c->sample.valid&AG_SMOKE))c->faults|=AG_FAULT_SMOKE_SENSOR;
  /* 温湿度是否在线由 board_port.c 的 AG_DHT_HOLD_MS 窗口判定，
     这里只看 sample.valid 里的 AG_AIR 位；再叠一层"air_at_ms 超过 5 秒"
     会把刚放宽到 20 秒的窗口重新压回 5 秒，拖一次参数照样跳报警。 */
  if(!fresh||!(c->sample.valid&AG_AIR))c->faults|=AG_FAULT_AIR;
  if(!fresh||!(c->sample.valid&AG_LIGHT))c->faults|=AG_FAULT_LIGHT;
 }

 if(fresh&&(c->sample.valid&AG_SMOKE)){
  /* 按 ppm 判：达到 500ppm 报警，一降到 500ppm 以下立刻解除，不留滞回。 */
  if(c->sample.smoke_ppm>=AG_SMOKE_PPM_ALARM)c->smoke_alarm=1;
  if(c->sample.smoke_ppm<AG_SMOKE_PPM_CLEAR)c->smoke_alarm=0;
 }
 if(fresh&&!(c->faults&AG_FAULT_AIR)){
  /* 温度过高：到 50℃ 报警，回落到 47℃ 以下解除（3 度滞回，防止在阈值上抖）。
     阈值放在 agri_control.h 的 AG_HOT_ALARM / AG_HOT_CLEAR，
     话术在 cane_runtime.c 的 HOT_ALARM_TEXT，两边不要各写一份数字。 */
  if(c->sample.temperature10>=AG_HOT_ALARM)c->hot_alarm=1;
  if(c->sample.temperature10<=AG_HOT_CLEAR)c->hot_alarm=0;
  /* 储存仓湿度上限 70%：到 70% 报警，回落到 68% 以下解除（留 2 个点滞回）。
     和温度一样先确认温湿度采集本身有效，读不到数时不误报"湿度过高"。 */
  if(c->sample.humidity10>=AG_HUMI_ALARM)c->humi_alarm=1;
  if(c->sample.humidity10<=AG_HUMI_CLEAR)c->humi_alarm=0;
 }
 if(c->smoke_alarm)c->faults|=AG_FAULT_SMOKE_HIGH;

 if(c->hot_alarm)c->faults|=AG_FAULT_AIR_HOT;
 /* 湿度过高单独占一位：屏幕、蜂鸣器、语音、云端都按这一位走。
     它不参与流程联锁（见 cane_runtime.c 的 environment_faults），
     湿了要报警要除湿，但不必把整条产线停下来。 */
 if(c->humi_alarm)c->faults|=AG_FAULT_HUMI_HIGH;
 /* 风扇不再按空气温度启停，由运行任务结合流程请求及本轮故障统一决定。 */
 c->fan=0;
 c->lamp=0;
}
