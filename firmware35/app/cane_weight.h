#ifndef CANE_WEIGHT_H
#define CANE_WEIGHT_H
#include <stdio.h>

/* ================================================================
   量纲约定（面板值 = 屏幕值，1:1）

   HX711 控制盘显示 100，屏幕就要显示 100。
     · 称重页：面板 100 -> "100.0 kg"
     · 分装页：面板 100 -> "100 g"

   平台 HX711 器件把面板 1 个单位放大成 42 个计数，
   cane_hal.c 里已用 /42 还原成面板值。

   内部 weight10 = 面板值 * 10，只是为了保留一位小数：
     面板 100 -> weight10 = 1000
     面板 100.3（若将来有小数）-> weight10 = 1003
   所以：
     · 转成面板值（整数位）        = weight10 / 10
     · 面板值的小数位              = weight10 % 10
   ================================================================ */

/* 按 kg 显示（称重页）：面板值本身就是 kg，保留一位小数。 */
static inline void cane_weight_kgu(char *b,unsigned n,unsigned weight10,const char *unit)
{
 snprintf(b,n,"%u.%u%s",weight10/10u,weight10%10u,unit);
}

/* 按 g 显示（分装页）：面板值本身就是 g，整克不拖小数尾巴。 */
static inline void cane_weight_g(char *b,unsigned n,unsigned weight10,const char *unit)
{
 if(weight10%10u==0u)snprintf(b,n,"%u%s",weight10/10u,unit);
 else snprintf(b,n,"%u.%u%s",weight10/10u,weight10%10u,unit);
}

/* 旧名字沿用，等同按克。 */
static inline void cane_weight_kg(char *b,unsigned n,unsigned weight10,const char *unit)
{ cane_weight_g(b,n,weight10,unit); }

/* 播报按克：整克读数字，带小数读“点X”。 */
static inline void cane_weight_speech(char *b,unsigned n,unsigned weight10)
{
 if(weight10%10u==0u)snprintf(b,n,"%u克",weight10/10u);
 else snprintf(b,n,"%u点%u克",weight10/10u,weight10%10u);
}

/* 播报按千克：面板 100 -> weight10=1000 -> “100千克”。 */
static inline void cane_mass_speech(char *b,unsigned n,unsigned weight10)
{
 if(weight10%10u==0u)snprintf(b,n,"%u千克",weight10/10u);
 else snprintf(b,n,"%u点%u千克",weight10/10u,weight10%10u);
}

/* 播报温度：内部 t10 单位 0.1℃，整度读“102度”，带小数读“36点5度”。
   负数（例如传感器没就绪给的哨兵值）统一读成“未就绪”，不念出 -3276 这种数字。 */
static inline void cane_temp_speech(char *b,unsigned n,int32_t t10)
{
 long v;
 if(t10<=-3000){snprintf(b,n,"未就绪");return;}
 v=(t10<0)?-(long)t10:(long)t10;
 if(v%10L==0L)snprintf(b,n,"%s%ld度",t10<0?"负":"",v/10L);
 else snprintf(b,n,"%s%ld点%ld度",t10<0?"负":"",v/10L,v%10L);
}
#endif
