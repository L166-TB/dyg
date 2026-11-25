/* 六道工序界面：白底高对比，单一配色（日夜切换已删除）。分区脏刷新，每次最多480像素。 */
#include "cane_gui.h"
#include "cane_weight.h"
#include "tft35.h"
#include "gui_font.h"
#include "speech.h"
#include <stdio.h>
#include <string.h>
typedef struct {uint16_t bg,panel,border,text,muted,accent,green,yellow,red,selected;} Palette;
/* 只有一套白底配色：电路上日夜切换键已删除，界面不再跟随主题。 */
static const Palette palettes[]={
 {0xffff,0xf7be,0xdedb,0x1927,0x528c,0x02d3,0x03c9,0x9ac0,0xb904,0xd71e}
};
static const Palette *palette=&palettes[0];
#define BG palette->bg
#define PANEL palette->panel
#define BORDER palette->border
#define TEXT palette->text
#define MUTED palette->muted
#define ACCENT palette->accent
#define GREEN palette->green
#define YELLOW palette->yellow
#define RED palette->red
typedef struct { uint16_t x,y,w,h; } GuiRect;
static const GuiRect zone[] = {
    {0,0,320,30},{0,30,72,420},{76,34,240,22},
    /* 3.5寸 320x480 竖屏：左侧工序栏 + 右侧 2x2 加高卡片，保持原白底/强调色。 */
    {76,64,116,140},{200,64,116,140},{76,210,116,140},{200,210,116,140},
    {76,356,240,36},{0,29,320,1},{76,398,240,40},{76,444,240,16}
};
/* 半行缓冲占240字节；两块拼成一行，保留原来的480像素刷新预算。 */
static uint16_t line[120], scan_x;
static uint32_t sig[11], pending_sig[11], frame_no, pixels_sent, regions_sent, last_log;
static int active = -1;
static uint16_t scan_y;
static uint8_t page,voice_busy;
static AgControl ag_view;
static CaneState cane_view;


static unsigned utf8_next(const char **s)
{
    const unsigned char *p=(const unsigned char *)*s; unsigned c=*p++;
    if(c>=0xe0 && c<0xf0 && p[0] && p[1]) { c=((c&15u)<<12)|((p[0]&63u)<<6)|(p[1]&63u); p+=2; }
    else if(c>=0xc0 && c<0xe0 && p[0]) { c=((c&31u)<<6)|(p[0]&63u); p++; }
    *s=(const char *)p; return c;
}
static void put_pixel(unsigned x,unsigned y,uint16_t color)
{ if(y==scan_y && x>=scan_x && x<scan_x+120u) line[x-scan_x]=color; }
static void fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color)
{ unsigned i; if(scan_y<y || scan_y>=y+h)return; for(i=x;i<x+w && i<320u;i++)put_pixel(i,scan_y,color); }
static void sized_text(unsigned x,unsigned y,const char *s,uint16_t color,unsigned numerator,unsigned denominator,unsigned bold)
{
 unsigned code,k,width,row,h=16u*numerator/denominator;const uint8_t *g;
 if(scan_y<y||scan_y>=y+h)return;
 row=(scan_y-y)*denominator/numerator;
 while(*s){code=utf8_next(&s);width=code<128u?8u:16u;g=gui_glyph(code);
  if(g)for(k=0;k<width*numerator/denominator;k++){
   unsigned source=k*denominator/numerator;
   if(g[row*(width==8u?1u:2u)+(width==8u?0u:source/8u)]&(1u<<(7u-(source&7u)))){
    put_pixel(x+k,scan_y,color);if(bold)put_pixel(x+k+1u,scan_y,color);
   }
  }x+=width*numerator/denominator;
 }
}
static void text(unsigned x,unsigned y,const char *s,uint16_t color)
{sized_text(x,y,s,color,1u,1u,0u);}
static void strong(unsigned x,unsigned y,const char *s,uint16_t color)
{sized_text(x,y,s,color,1u,1u,1u);}
static void card(const GuiRect *r,uint16_t color)
{ fill(r->x+2,r->y,r->w-4,r->h,color); fill(r->x,r->y+2,r->w,r->h-4,color); }
static void value10(char *b,unsigned n,int v,const char *unit)
{ int a=v<0?-v:v; snprintf(b,n,"%s%d.%d%s",v<0?"-":"",a/10,a%10,unit); }

/* 顶部状态栏一句话：优先看安全，其次看当前工序报警。 */
static const char *status_label(const AgControl *a,const CaneState *c)
{
 if(c->sample.fire)return "明火报警";
 if(c->sample.person)return "人员进入危险区";
 if(a->faults&AG_FAULT_SMOKE_HIGH)return "MQ2烟雾超限";
 if(a->faults&AG_FAULT_AIR_HOT)return "温度过高";
 /* 控制调度超时不进顶栏：它只反映软件节拍抖动，
    显示出来只会让人以为产线出事，实际数据照常刷新。 */
 /* 储存仓湿度上限 70%，超限时顶栏直接报出来，不用翻到入库页看。 */
 if(a->faults&AG_FAULT_HUMI_HIGH)return "储存仓湿度过高";
 
 if(c->proc.alarm)return cane_alarm_text(c->proc.stage,c->proc.alarm);
 if(c->faults&2u)return "称重采集未就绪";
 if(c->faults&1u)return "测温采集未就绪";
 if(c->faults&4u)return "设备测距未就绪";
 if(c->faults&8u)return "霍尔采集未就绪";
 if(c->faults)return "设备反馈超时";
 if(a->faults&AG_FAULT_SMOKE_SENSOR)return "MQ2采集未就绪";

 /* AG_FAULT_AIR（温湿度探头没读到帧）不在顶栏显红：
     它只是"暂时读不到"，卡片上已写"未就绪"，顶栏再报一遍会让操作员
     以为整条线有事。详见 cane_runtime.c 里 alarm_reason 的说明。 */
 /* 只剩 AG_FAULT_TIMING 时别报"环境反馈需检查"——那会让操作员以为
    传感器坏了。软件节拍抖动不影响数据刷新，顶栏按正常状态显示。 */
 if(a->faults&~AG_FAULT_TIMING)return "环境反馈需检查";
 return c->proc.run?"手动推进：点下一步":"流程已暂停";
}
static void button(unsigned x,unsigned w,const char *label,uint16_t color)
{fill(x,398,w,40,PANEL);fill(x,437,w,1,BORDER);strong(x+10,408,label,color);}
static void draw_buttons(const CaneState *c)
{
 if(c->page==PAGE_TARGET){button(76,76,"减少",TEXT);button(156,76,"选择",ACCENT);button(236,76,"增加",TEXT);}
 else if(c->page==PAGE_SAFETY){button(76,116,"返回工序",ACCENT);button(200,116,"确认报警",TEXT);}
 else if(c->page==PAGE_COMMS){button(76,116,"播报本页",ACCENT);button(200,116,"返回流程",TEXT);}
 else{button(76,116,c->proc.run?"暂停流程":"继续运行",ACCENT);button(200,116,c->proc.stage==PSTAGE_STORE?"下一批":"下一步",TEXT);}
}
/* 每张卡显示本工序一个实际读数，四张卡覆盖该工序全部关键量。 */
static void draw_card(unsigned n)
{
 const AgControl *a=&ag_view;const CaneState *c=&cane_view;const CaneSample *s=&c->sample;const CaneProcess *p=&c->proc;
 const GuiRect *r=&zone[3+n];char b[32];int valid=1,pending;uint16_t color=TEXT;
 static const char *const labels[6][4]={
  {"原料净重","称重目标","入料闸门","称重采集"},
  {"甘蔗液位","液位上限","榨汁电机","液体闸门"},
  {"糖浆温度","目标温度","冷却风扇","测温采集"},
  {"每份重量","目标/上限","分装阀位","完成份数"},
  {"糖浆温度","冷却风扇","复核重量","完成批次"},
  {"储存仓温度","储存仓湿度","环境判定","入库状态"}
 };
 unsigned pg=c->page>=PAGE_WEIGH&&c->page<=PAGE_STORE?c->page-PAGE_WEIGH:0;
 card(r,c->page==PAGE_TARGET&&c->selected==n?palette->selected:PANEL);
 fill(r->x,r->y+4,2,r->h-8,c->page==PAGE_TARGET&&c->selected==n?ACCENT:BORDER);
 if(c->page==PAGE_TARGET){
  static const char *l[]={"称重目标 kg","每份糖浆 g","煮糖温度 C","冷却温度 C"};
  strong(r->x+8,r->y+12,l[n],MUTED);
  if(n==0)cane_weight_kgu(b,sizeof(b),(unsigned)p->t_weigh10,"");
  else if(n==1)cane_weight_g(b,sizeof(b),(unsigned)p->t_fill10,"");
  else if(n==2)value10(b,sizeof(b),p->t_boil10,"");
  else value10(b,sizeof(b),p->t_cool10,"");
  if(n==0)sized_text(r->x+8,r->y+56,b,ACCENT,2u,1u,1u);else strong(r->x+8,r->y+60,b,ACCENT);return;
 }
 if(c->page==PAGE_SAFETY){
  static const char *l[]={"明火检测","人员检测","MQ2浓度","反馈状态"};
  strong(r->x+8,r->y+12,l[n],MUTED);
  if(n<2){unsigned danger=n?s->person:s->fire;color=danger?RED:GREEN;strcpy(b,danger?"危险":"正常");}
  else if(n==2){snprintf(b,sizeof(b),"%u ppm",(unsigned)a->sample.smoke_ppm);valid=(a->sample.valid&AG_SMOKE)!=0;color=a->smoke_alarm?RED:TEXT;}
  else{pending=c->faults||(a->faults&~AG_FAULT_TIMING);strcpy(b,pending?"需检查":"正常");color=pending?RED:GREEN;}
  if(n==0)sized_text(r->x+8,r->y+56,valid?b:"未就绪",valid?color:YELLOW,2u,1u,1u);else strong(r->x+8,r->y+60,valid?b:"未就绪",valid?color:YELLOW);return;
 }
 if(c->page==PAGE_COMMS){
  static const char *l[]={"BC26通信","中文播报","云平台","系统状态"};
  strong(r->x+8,r->y+12,l[n],MUTED);
  strcpy(b,n==0?cane_cloud_status():n==1?(voice_busy?"播报中":"就绪"):n==2?"OneNET":(c->alarm?"报警":"正常"));
  if(n==0)color=c->wifi?GREEN:MUTED;
  if(n==3)color=c->alarm?RED:GREEN;
  if(n==0)sized_text(r->x+8,r->y+56,b,color,2u,1u,1u);else strong(r->x+8,r->y+60,b,color);return;
 }
 {const char *label=labels[pg][n];
  /* 提纯段第一张卡只写“浑浊度”，不再带“蔗糖汁”前缀：三字正好放得下，
     也不会因为卡片宽度被截成“蔗糖汁　度”。 */
  if(pg==1&&p->stage==PSTAGE_PURIFY_FINE){static const char *fine[]={"浑浊度","标准上限","过滤阀门","电机状态"};label=fine[n];}
  strong(r->x+8,r->y+10,label,TEXT);
 }
 switch(pg){
 case 0:
  /* 称重页按 kg 显示：原料 500kg 进场。 */
  if(n==0){cane_weight_kgu(b,sizeof(b),s->weight10," kg");valid=(s->valid&CANE_WEIGHT)!=0;color=ACCENT;}
  else if(n==1)cane_weight_kgu(b,sizeof(b),(unsigned)p->t_weigh10," kg");
  else if(n==2){strcpy(b,p->feeder?"运行":"停止");color=p->feeder?GREEN:MUTED;}
  else{strcpy(b,(s->valid&CANE_WEIGHT)?"有效":"未就绪");color=(s->valid&CANE_WEIGHT)?GREEN:YELLOW;}
  break;
 case 1:
  if(p->stage==PSTAGE_PURIFY_FINE){
   /* 提纯页：浑浊度进工序固定 45%，按 PC13 开阀后随过滤时间降到 10%。
      第三卡是过滤阀状态：未打开 -> 已打开 -> 过滤完成，颜色跟着状态走。 */
   if(n==0){snprintf(b,sizeof(b),"%u %%",p->conc);color=p->conc<=CANE_CONC_CLEAN?GREEN:YELLOW;}
   else if(n==1)snprintf(b,sizeof(b),"<= %u %%",p->t_conc);
   else if(n==2){
    if(p->filter_done){strcpy(b,"已完成");color=GREEN;}
    else if(p->filter_open){strcpy(b,"已打开");color=GREEN;}
    else{strcpy(b,"未打开");color=YELLOW;}
   }
   else snprintf(b,sizeof(b),"%u %%",p->stir);
  }else{
   /* 榨汁页：甘蔗液位按超声波实测/时间斜坡算出的厘米数，0→180cm。 */
   if(n==0){snprintf(b,sizeof(b),"%u cm",p->juice_cm);color=ACCENT;}
   else if(n==1)snprintf(b,sizeof(b),"%u cm",CANE_JUICE_FULL_CM);
   else if(n==2)snprintf(b,sizeof(b),"%u %%",p->stir);
   else strcpy(b,p->valve?"已放液":"关闭");
  }
  break;
 case 2:
  if(n==0){value10(b,sizeof(b),s->object10," C");valid=(s->valid&CANE_IR)!=0;color=ACCENT;}
  else if(n==1)snprintf(b,sizeof(b),">= %u C",p->t_boil10/10);
  else if(n==2){snprintf(b,sizeof(b),"%u %%",c->fan);color=c->fan2?ACCENT:MUTED;}
  else{strcpy(b,(s->valid&CANE_IR)?"有效":"未就绪");color=(s->valid&CANE_IR)?GREEN:YELLOW;}
  break;
 case 3:
  /* 分装页按 g 显示：每份目标 25g，单份上限 200g。 */
  if(n==0){cane_weight_g(b,sizeof(b),s->weight10," g");valid=(s->valid&CANE_WEIGHT)!=0;color=ACCENT;}
  else if(n==1)snprintf(b,sizeof(b),"%u/%u g",p->t_fill10/10,p->t_fill_max10/10);
  else if(n==2){strcpy(b,p->valve?"打开":"关闭");color=p->valve?GREEN:MUTED;}
  else{snprintf(b,sizeof(b),"%u/%u",p->fill_count,p->fill_total);color=p->fill_count>=p->fill_total?GREEN:TEXT;}
  break;
 case 5:
  /* 糖块入库页：PA0 的 DHT11_1 由环境采集任务读取，这里只做显示。 */
  if(n==0){value10(b,sizeof(b),a->sample.temperature10," C");valid=(a->sample.valid&AG_AIR)!=0;color=ACCENT;}
  else if(n==1){snprintf(b,sizeof(b),"%u %%",a->sample.humidity10/10u);valid=(a->sample.valid&AG_AIR)!=0;
   /* 超过 70% 上限显示红色，和其他报警卡片保持同一套视觉。 */
   color=(a->faults&AG_FAULT_HUMI_HIGH)?RED:TEXT;}
  else if(n==2){
   /* 第三张卡原来写“有效/未就绪”，这是操作员最常看到“未就绪”字样的地方，
      现场反馈不要这个说法。改成显示储存仓的判定结论：
      正常就报“正常”，超温报“温度过高”，潮湿报“需防潮”。
      判据全部来自环境任务已经算好的故障位，不在这里重新比阈值。 */
   if(a->faults&AG_FAULT_AIR_HOT){strcpy(b,"温度过高");color=RED;}
   else if(a->faults&AG_FAULT_HUMI_HIGH){strcpy(b,"需防潮");color=RED;}
   else{strcpy(b,"正常");color=GREEN;}
   /* 探头暂时读不到时第一、二张卡会保留最后一次读数，这一格就不下结论。 */
   if(!(a->sample.valid&AG_AIR)){b[0]=0;color=MUTED;}}
  else{strcpy(b,p->batch?"已入库":"待入库");color=GREEN;}
  break;
 default:
  if(n==0){value10(b,sizeof(b),s->object10," C");valid=(s->valid&CANE_IR)!=0;color=ACCENT;}
  else if(n==1){snprintf(b,sizeof(b),"%u %%",c->fan);color=c->fan2?ACCENT:MUTED;}
  else if(n==2){cane_weight_g(b,sizeof(b),s->weight10," g");valid=(s->valid&CANE_WEIGHT)!=0;}
  else{snprintf(b,sizeof(b),"BATCH %u",p->batch);color=GREEN;}
  break;
 }
 if(n==0)sized_text(r->x+8,r->y+56,valid?b:"未就绪",valid?color:YELLOW,2u,1u,1u);else strong(r->x+8,r->y+60,valid?b:"未就绪",valid?color:YELLOW);
}
static uint32_t zone_sig(unsigned z,const AgControl *a,const CaneState *c,uint32_t f)
{
 uint32_t v;const CaneProcess *p=&c->proc;
 if(z==0)return c->wifi|((uint32_t)p->run<<16)|((uint32_t)c->alarm<<17);
 if(z==1)return c->page;
 if(z==2)return c->page|((uint32_t)p->stage<<8)|((uint32_t)c->selected<<16);
 if(z>=3&&z<=6){
  unsigned n=z-3;
  if(c->page==PAGE_TARGET)v=n==0?(uint16_t)p->t_weigh10:n==1?(uint16_t)p->t_fill10:n==2?(uint16_t)p->t_boil10:(uint16_t)p->t_cool10;
  else if(c->page==PAGE_WEIGH)v=n==0?c->sample.weight10:n==1?(uint32_t)p->t_weigh10:n==2?p->feeder:(c->sample.valid&CANE_WEIGHT);
  else if(c->page==PAGE_PURIFY)v=n==0?(p->stage==PSTAGE_PURIFY_FINE?p->conc:p->juice_cm)
    :n==1?(p->stage==PSTAGE_PURIFY_FINE?p->t_conc:(uint32_t)CANE_JUICE_FULL_CM)
    :n==2?p->stir:(p->valve|((uint32_t)p->filter_open<<8)|((uint32_t)p->stage<<16));
  else if(c->page==PAGE_BOIL)v=n==0?(uint16_t)c->sample.object10:n==1?(uint16_t)p->t_boil10:n==2?c->fan:(c->sample.valid&CANE_IR);
  else if(c->page==PAGE_FILL)v=n==0?c->sample.weight10:n==1?(uint16_t)p->t_fill10:n==2?p->valve:(p->fill_count|(p->fill_total<<8));
  else if(c->page==PAGE_COOL)v=n==0?(uint16_t)c->sample.object10:n==1?c->fan:n==2?c->sample.weight10:p->batch;
  else if(c->page==PAGE_STORE)v=n==0?(uint16_t)a->sample.temperature10
    :n==1?(a->sample.humidity10|((a->faults&AG_FAULT_HUMI_HIGH)<<20))
    :n==2?(a->sample.valid&AG_AIR):p->batch;
  else if(c->page==PAGE_SAFETY)v=n==0?c->sample.fire:n==1?c->sample.person:n==2?a->sample.smoke_ppm:(uint16_t)(c->faults|(a->faults&~AG_FAULT_TIMING));
  else v=n==0?(uint32_t)(unsigned char)cane_cloud_status()[0]:n==1?voice_busy:n==2?1u:(uint32_t)c->alarm;
  return(v*16777619u)^((uint32_t)c->page<<24)^((uint32_t)c->selected<<16)^((uint32_t)p->stage<<20)^c->sample.valid^((uint32_t)a->sample.valid<<8);
 }
 if(z==7)return (c->faults*16777619u)^((uint32_t)c->sample.fire<<5)^((uint32_t)c->sample.person<<6)^((uint32_t)p->alarm<<20)^((a->faults&~AG_FAULT_TIMING)<<8)^((uint32_t)p->run<<25)^((c->alarm?f&1u:0u)<<24);
 if(z==8)return c->fan|((uint32_t)c->fan2<<9)|((uint32_t)p->stir<<8)|((uint32_t)p->valve<<16)|((uint32_t)p->feeder<<17)|((c->fan||c->fan2||p->stir)?(f&1u)<<24:0u);
 if(z==9)return c->page|((uint32_t)p->run<<8)|((uint32_t)p->stage<<16);
 return c->page;
}
static void draw_zone(unsigned z)
{
 const AgControl *a=&ag_view;const CaneState *c=&cane_view;const CaneProcess *p=&c->proc;
 const GuiRect *r=&zone[z];char b[40];unsigned i;
 if(z==0){fill(0,0,320,30,BG);fill(8,8,4,14,ACCENT);text(20,7,"甘蔗榨糖产线",TEXT);
  fill(126,12,5,5,c->alarm?RED:(p->run?GREEN:MUTED));text(138,7,p->run?"运行中":"已暂停",ACCENT);
  text(190,7,"参数",ACCENT);text(222,7,"安全",ACCENT);text(254,7,"通信",ACCENT);text(286,7,"去皮",ACCENT);
  fill(0,29,320,1,BORDER);return;}
 if(z==1){
  /* 3.5寸竖屏：左侧竖向工序栏，六项各 60px。 */
  static const char *tabs[]={"称重","榨提","煮糖","分装","冷却","入库"};
  unsigned tab=c->page<=PAGE_STORE?c->page:0;
  fill(0,30,72,420,BG);fill(71,30,1,420,BORDER);
  for(i=0;i<6;i++){unsigned y=36u+i*60u;if(tab==i+1)fill(0,y,71,60,palette->selected);
   text(20,y+21,tabs[i],tab==i+1?ACCENT:MUTED);if(tab==i+1)fill(6,y+56,60,2,ACCENT);}
  return;
 }
 if(z==2){
  if(c->page<=PAGE_STORE){
   if(c->page==PAGE_PURIFY)snprintf(b,sizeof(b),"%s",p->stage==PSTAGE_PURIFY_FINE?"榨汁提纯 / 杂质提纯":"榨汁提纯 / 榨汁入液");
   else if(c->page==PAGE_STORE)strcpy(b,"糖块入库 / 储存仓温湿度");
   else snprintf(b,sizeof(b),"%s",cane_stage_name(c->page));
  }
  else if(c->page==PAGE_TARGET)strcpy(b,"参数设置 / 目标值");
  else if(c->page==PAGE_SAFETY)strcpy(b,"安全检测 / 具体原因");
  else strcpy(b,"数据上传 / 语音反馈");
  strong(76,38,b,TEXT);return;
 }
 if(z>=3&&z<=6){draw_card(z-3);return;}
 if(z==7){
  /* 状态灯同样不看 AG_FAULT_TIMING：软件节拍抖动不该把状态灯变红。 */
  int bad=c->alarm||(a->faults&~AG_FAULT_TIMING);
  card(r,PANEL);
  fill(r->x+8,r->y+15,5,5,bad?(frame_no&1u?RED:YELLOW):GREEN);
  strong(r->x+20,r->y+9,status_label(a,c),bad?RED:GREEN);return;
 }
 if(z==8){fill(0,29,320,1,BORDER);return;}
 if(z==9)draw_buttons(c);
 else{sized_text(76,445,"参数",ACCENT,3u,4u,0u);sized_text(136,445,"安全",ACCENT,3u,4u,0u);
  sized_text(196,445,"通信",ACCENT,3u,4u,0u);sized_text(256,445,"去皮",ACCENT,3u,4u,0u);}
}
void gui_invalidate(void){unsigned i;for(i=0;i<11;i++)sig[i]=0xffffffffu;active=11;scan_y=scan_x=0;}
void gui_init(void){page=0;palette=&palettes[0];gui_invalidate();frame_no=pixels_sent=regions_sent=0;}
void gui_render(const AgControl *a,const CaneState *c,uint32_t now)
{
 unsigned i,n,burst;uint32_t f=now/250u;voice_busy=speech_busy();
 /* 报警时把单帧像素预算从 480 提到 2880：整屏 480 行按 480/帧要走 160 帧，
    界面任务 20ms 一拍就是 3.2 秒——报警翻页等 3 秒等于没有报警。
    危险状态下 6 倍速（约 0.5 秒刷满），平时仍按 480 走，不抢流程页。 */
 burst=(a->smoke_alarm||(a->faults&~AG_FAULT_TIMING)||c->alarm||c->sample.fire||c->sample.person)?24u:4u;
 if(page!=c->page){page=c->page;gui_invalidate();}
 if(active<0)for(i=0;i<11;i++)if(sig[i]!=zone_sig(i,a,c,f)){active=(int)i;scan_y=zone[i].y;scan_x=zone[i].x;break;}
 if(active>=0){const GuiRect *r=active==11?0:&zone[active];
  if(scan_y==(r?r->y:0)&&scan_x==(r?r->x:0)){ag_view=*a;cane_view=*c;frame_no=f;regions_sent++;for(i=0;i<11;i++)pending_sig[i]=zone_sig(i,a,c,f);}
  for(n=0;n<burst;n++){unsigned x=r?r->x:0,end=r?r->x+r->w:320,w=end-scan_x;
   if(w>120u)w=120u;
   for(i=0;i<w;i++)line[i]=BG;
   for(i=active==11?0u:(unsigned)active;i<(active==11?11u:(unsigned)active+1u);i++)draw_zone(i);
   tft35_span(scan_x,scan_y,line,w);pixels_sent+=w;scan_x+=w;
   if(scan_x>=end){scan_x=x;scan_y++;}
   if(scan_y>=(r?r->y+r->h:480)){if(active==11)for(i=0;i<11;i++)sig[i]=pending_sig[i];else sig[active]=pending_sig[active];active=-1;break;}
  }
 }
 if(now-last_log>=5000u){last_log=now;printf("GUI35,page=%u,pixels=%lu,regions=%lu,budget=%u\n",page,(unsigned long)pixels_sent,(unsigned long)regions_sent,(unsigned)(burst*120u));}
}
int gui_touch_action(unsigned p,uint16_t x,uint16_t y)
{
 if(y<30&&x>=184){if(x<216)return GUI_TARGET;if(x<248)return GUI_SAFETY;if(x<280)return GUI_COMMS;return GUI_TARE;}
 if(x<72&&y>=36&&y<396){unsigned t=1u+(y-36u)/60u;return t>6?GUI_NONE:(int)t;}
 if(p==PAGE_TARGET&&x>=76&&x<316&&y>=64&&y<350){
  unsigned n=(y>=210?2u:0u)+(x>=200?1u:0u);return GUI_SELECT0+(int)n;
 }
 if(y>=398&&y<438){
  if(p==PAGE_TARGET)return x<156?GUI_MINUS:x<236?GUI_ENTER:GUI_PLUS;
  if(p==PAGE_SAFETY)return x<200?GUI_ENTER:GUI_ACK;
  if(p==PAGE_COMMS)return x<200?GUI_SPEAK:PAGE_WEIGH;
  return x<200?(cane_view.proc.run?GUI_STOP:GUI_START):GUI_STEP;
 }
 if(y>=438){
  if(x<136)return GUI_TARGET;
  if(x<196)return GUI_SAFETY;
  if(x<256)return GUI_COMMS;
  return GUI_TARE;
 }
 return GUI_NONE;
}
