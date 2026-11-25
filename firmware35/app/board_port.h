#ifndef BOARD_PORT_H
#define BOARD_PORT_H
#include "agri_control.h"
#include "board_hal.h"
/* MQ2 经两枚10k分压。平台实测：组件参数 192（洁净空气）→ ADC253，参数 10000 → ADC869。
   参数本身就是 MQ-2 的 ppm 量程，ppm 换算见 ag_smoke_ppm()。 */
#define AG_SMOKE_CLEAR_ADC AG_SMOKE_ADC_BASE
#define AG_SMOKE_FULL_ADC  AG_SMOKE_ADC_FULL
#define AG_LIGHT_DARK_ADC 1365u
#define AG_LIGHT_BRIGHT_ADC 2730u
/* 原料仓探头满/空距离为演示几何，不能当真实容量或吨位。 */
#define AG_HOPPER_FULL_MM 100u
#define AG_HOPPER_EMPTY_MM 4000u
#define AG_HOPPER_MARGIN_MM 50u
/* 采样节拍：烟感属于安全量，2 秒一拍意味着烟起来要等 2 秒才上屏；
   改成 250ms 一拍，浓度一变当拍就能抓进来。DHT22 是单总线器件，
   两次读取必须间隔 >=2s，所以它单独用 AG_DHT_PERIOD_MS 节流。 */
#define AG_SAMPLE_PERIOD_MS 250u
#define AG_DHT_PERIOD_MS   2000u
/* 温湿度有效窗 20s：DHT11 单总线在平台上很脆，操作员拖动温度参数的那几秒
   时序会被打断，连续几帧都解不出来。窗口给足 20 秒，拖完参数两秒内下一帧
   就能接上，期间屏幕继续显示最后一次读到的真实值——既不闪"未就绪"，
   也不会因为一次坏帧就判成"探头没接"。 */
#define AG_DHT_HOLD_MS     20000u
#define AG_CONTROL_PERIOD_MS 50u
#define AG_UI_PERIOD_MS 20u
#define AG_RENDER_PERIOD_MS 1000u
void board_safe_init(void);
void board_sensor_init(void);
void board_read_sample(AgSample *s,uint32_t now);
void board_apply(const AgControl *c);
void board_all_off(void);
/* PA7 = 榨汁/搅拌直流电机开关量输出：motor 非零拉高、为零拉低。
   不用 PWM，因为平台 dcmotor 模型按 2kHz 采样引脚电平，认不出 20kHz PWM。 */
void pa7_apply(unsigned motor);
uint8_t board_buttons(void);
void board_display_init(void);
void board_display(const AgControl *c);
#endif
