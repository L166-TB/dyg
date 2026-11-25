/* OneNET联调：凭据从正常调试串口临时配置，只上传真实传感数据。 */
#include "cane.h"
#include "board_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "cane_cloud_json.h"

#define RX_SIZE 2048u
/* 五组约5秒完成一轮，避免旧10秒组间等待使整轮超过手机30秒有效期。 */
#define REPORT_GAP_MS 1000u
typedef struct {volatile uint16_t head,tail;volatile uint32_t drops,overruns,errors,received;uint8_t bytes[RX_SIZE];} SerialRing;
static SerialRing module_rx,config_rx;
static char config_line[640],module_line[1800],host[128],product[32],device[64],token[384];
static char command[2300],payload[900],response_topic[180],response_payload[520];
static unsigned config_used,line_used,config_discard,line_discard,initialized,configured;
static unsigned step,waiting,connected,attempts,stopped,packet_used,packet_fields,attached,group,packet_failed,response_pending;
static uint16_t port=1883,packet_id=10;
static const AgControl *current_ag;
static unsigned executing,executing_kind,tx_response,cache_at;
static int32_t executing_values[4];
static char active_id[65],incoming_json[640];
static struct {char id[65];int result;} cache[8];
static uint32_t diagnostics_at,rx_lines,rx_messages,rx_overflows;
/* 1表示曾经在线但接收通道失效，供界面区分“未配置”和“接收异常”。 */
static unsigned link_fault;
static uint32_t sent_at,retry_at,publish_at,report_sent_at,sequence,pending_report,accepted_reports,ack_timeouts,subscription_at;
enum {S_AT,S_ECHO,S_WAKE,S_ATTACH,S_CHECK_ATTACH,S_VERSION,S_KEEPALIVE,S_SESSION,S_LENGTH,S_OPEN,S_LOGIN,S_SUB,S_SUB_REJECT,S_SUB_CMD,S_IDLE,S_PUBLISH,S_DATA,S_ACK,S_DISC,S_CLOSE,S_STATUS};
static void rx_irq(USART_TypeDef *uart,SerialRing *r)
{
 unsigned budget=64;
 /* 先读状态再读数据，清除ORE/FE/NE；单次中断有预算，避免错误中断饿死保护任务。 */
 while(budget--){
  uint32_t status=uart->SR;uint8_t b;uint16_t next;
  if(!(status&(USART_SR_RXNE|USART_SR_ORE|USART_SR_FE|USART_SR_NE)))break;
  b=(uint8_t)uart->DR;
  if(status&USART_SR_ORE)r->overruns++;
  if(status&(USART_SR_FE|USART_SR_NE))r->errors++;
  if(!(status&USART_SR_RXNE))continue;
  r->received++;next=(r->head+1u)&(RX_SIZE-1u);
  if(next==r->tail)r->drops++;else{r->bytes[r->head]=b;r->head=next;}
 }
}
void USART2_IRQHandler(void){rx_irq(USART2,&module_rx);}
void USART1_IRQHandler(void){rx_irq(USART1,&config_rx);}
unsigned cane_wifi_connected(void){return connected;}
unsigned cane_wifi_receive_fault(void){return link_fault&&!connected;}
const char *cane_cloud_status(void)
{
 if(!configured)return "待配置";
 if(stopped)return "连接失败";
 if(link_fault&&!connected)return "接收异常";
 if(connected&&step==S_IDLE)return "在线";
 if(connected)return "通信中";
 return "连接中";
}
static void wipe(char *s,unsigned n){volatile char *p=s;while(n--)*p++=0;}
static int uart_send(const char *s,unsigned n)
{
 unsigned i;for(i=0;i<n;i++){
  uint32_t at=board_millis();
  /* 让出任务执行权，避免短微秒超时因调度抢占截断AT命令。 */
  while(!USART_GetFlagStatus(USART2,USART_FLAG_TXE)){if((uint32_t)(board_millis()-at)>100u)return -1;LOS_TaskDelay(1);}
  USART_SendData(USART2,(uint8_t)s[i]);
 }return 0;
}
static int identifier(const char *s,unsigned max)
{unsigned i,n=strlen(s);if(!n||n>=max)return 0;for(i=0;i<n;i++)if(!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-",s[i]))return 0;return 1;}
static void configure(uint32_t now)
{
 char *parts[6],*p=config_line;unsigned i;char *end;unsigned long number;
 /* 配置从USART1 RX接收，不读取或写入游戏内部状态。 */
 for(i=0;i<6;i++){parts[i]=p;p=strchr(p,'|');if(i<5){if(!p)goto reject;*p++=0;}else if(p)goto reject;}
 if(strcmp(parts[0],"CLOUDCFG")||!identifier(parts[1],sizeof(host))||!identifier(parts[3],sizeof(product))||!identifier(parts[4],sizeof(device)))goto reject;
 number=strtoul(parts[2],&end,10);if(*end||!number||number>65535u)goto reject;
 if(strlen(parts[5])>=sizeof(token)||strncmp(parts[5],"version=2018-10-31&",18))goto reject;
 for(p=parts[5];*p;p++)if(!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%&=_-./:+",*p))goto reject;
 strcpy(host,parts[1]);port=(uint16_t)number;strcpy(product,parts[3]);strcpy(device,parts[4]);
 wipe(token,sizeof(token));strcpy(token,parts[5]);configured=1;connected=waiting=attempts=stopped=0;link_fault=0;step=S_CLOSE;retry_at=now;pending_report=0;ack_timeouts=0;
 printf("CLOUD,CONFIG_READY,host=%s,port=%u,product=%s,device=%s\n",host,port,product,device);
 wipe(config_line,sizeof(config_line));return;
reject:
 printf("CLOUD,CONFIG_REJECTED\n");wipe(config_line,sizeof(config_line));
}
static void fail_link(uint32_t now,const char *why)
{
 unsigned was_connected=connected;
 connected=waiting=0;pending_report=0;link_fault=was_connected?1u:link_fault;attempts++;
 if(attempts>=3u){stopped=1;printf("CLOUD,STOPPED,reason=%s,attempts=%u\n",why,attempts);}
 else{step=was_connected?S_STATUS:S_CLOSE;retry_at=now+1000u*attempts;printf("CLOUD,RETRY,reason=%s,attempt=%u\n",why,attempts);}
 wipe(command,sizeof(command));
}
static void next(void){step++;waiting=0;}
static const char *result_text(int r)
{switch(r){case 0:return "执行成功";case 1:return "参数错误";case 2:return "安全联锁阻止";case 3:return "传感器未就绪";case 4:return "流程状态不允许";case 5:return "去皮未确认";case 9:return "设备暂时忙";default:return "等待确认";}}
static void make_response(int result)
{
 snprintf(response_topic,sizeof(response_topic),"$sys/%s/%s/cmd/response/%s",product,device,active_id);
 snprintf(response_payload,sizeof(response_payload),"{\"result_code\":%d,\"result_desc\":\"%s\"}",result,result_text(result));
 response_pending=1;executing=0;
 strncpy(cache[cache_at].id,active_id,64);cache[cache_at].id[64]=0;cache[cache_at].result=result;cache_at=(cache_at+1u)%8u;
 printf("CLOUD,COMMAND_RESULT,id=%s,result=%d\n",active_id,result);
}
static void command_received(uint32_t now,const char *topic,const char *body)
{
 unsigned i;int result;char expected[160];const char *id;
 snprintf(expected,sizeof(expected),"$sys/%s/%s/cmd/request/",product,device);
 if(strncmp(topic,expected,strlen(expected)))return;
 id=topic+strlen(expected);
 if(!identifier(id,65))return;
 /* 单飞命令等待真实完成；重复ID只能重发原结果，不能再次去皮或启动。 */
 if(executing||response_pending)return;
 strcpy(active_id,id);
 for(i=0;i<8;i++)if(!strcmp(cache[i].id,id)){make_response(cache[i].result);return;}
 if(!cane_cloud_parse(body,&executing_kind,executing_values)){make_response(1);return;}
 result=cane_cloud_command(executing_kind,executing_values,1,current_ag);
 if(result==10)executing=1;else make_response(result);
 (void)now;
}
static void line_received(uint32_t now)
{
 int id,result,code,qos;unsigned message;char *p;
 rx_lines++;
 if(waiting&&step==S_STATUS){
  if(sscanf(module_line,"+QMTCONN: %d,%d",&id,&result)==2&&id==0)printf("CLOUD,SESSION_STATE,state=%d\n",result);
  if(!strcmp(module_line,"OK")||!strcmp(module_line,"ERROR")){waiting=0;step=S_DISC;return;}
 }
 if(waiting&&step==S_DISC){
  if(sscanf(module_line,"+QMTDISC: %d,%d",&id,&result)==2&&id==0){
   printf("CLOUD,DISCONNECT,result=%d\n",result);
   waiting=0;step=result==0?S_LOGIN:S_CLOSE;retry_at=now+1000u;return;
  }
  if(!strcmp(module_line,"ERROR")){waiting=0;step=S_CLOSE;return;}
 }
 if(waiting&&step==S_CLOSE){
  if(sscanf(module_line,"+QMTCLOSE: %d,%d",&id,&result)==2&&id==0){
   printf("CLOUD,CLOSE,result=%d\n",result);
   if(result!=0&&link_fault){stopped=1;waiting=0;printf("CLOUD,STOPPED,reason=CLOSE_FAILED\n");return;}
   waiting=0;step=S_AT;return;
  }
  /* 首次配置尚未建立socket时ERROR是可接受的空状态，不冒充关闭成功。 */
  if(!strcmp(module_line,"ERROR")){printf("CLOUD,CLOSE_NO_SOCKET\n");waiting=0;step=S_AT;return;}
 }
 if(strstr(module_line,"+QMTSTAT:")){fail_link(now,"MQTT_DISCONNECTED");return;}
 if(strncmp(module_line,"+QMTRECV:",9)==0){
  rx_messages++;p=strchr(module_line,'"');if(p){char *end=strchr(p+1,'"');
   if(end){char *body=strchr(end+1,'"');*end=0;
    if(strstr(p+1,"/dp/post/json/accepted")){char *j=strstr(end+1,"\"id\"");if(j&&(j=strchr(j,':'))&&sscanf(j+1,"%u",&message)==1&&message==pending_report){accepted_reports++;pending_report=0;ack_timeouts=0;printf("CLOUD,REPORT_ACCEPTED,id=%u,total=%lu\n",message,(unsigned long)accepted_reports);}}
    else if(strstr(p+1,"/dp/post/json/rejected")){printf("CLOUD,REPORT_REJECTED,id=%lu\n",(unsigned long)pending_report);pending_report=0;}
    else if(strstr(p+1,"/cmd/request/")&&body){char *last=strrchr(body+1,'"');if(last&&last>body&&last-body<sizeof(incoming_json)){unsigned n=(unsigned)(last-body-1);memcpy(incoming_json,body+1,n);incoming_json[n]=0;command_received(now,p+1,incoming_json);}}
   }
  }return;
 }
 if(!waiting)return;
 if(step==S_LENGTH&&strncmp(module_line,"+QMTPUB:",8)==0){printf("CLOUD,PUBLISH_CAPABILITY,%s\n",module_line);return;}
 if(strstr(module_line,"ERROR")){
  /* 仿真模块可能不支持回显、休眠及显示格式配置；仅这些可选项允许跳过。 */
  if(step==S_ECHO||step==S_WAKE||step==S_KEEPALIVE){printf("CLOUD,OPTION_UNSUPPORTED,step=%u\n",step);next();return;}
  printf("CLOUD,AT_ERROR,step=%u\n",step);fail_link(now,"AT_ERROR");return;
 }
 /* 查询数据和结尾OK均收到后再推进，避免残余OK误确认下一条命令。 */
 if(step==S_CHECK_ATTACH){
  if(sscanf(module_line,"+CGATT: %d",&result)==1){if(result==1)attached=1;else fail_link(now,"NOT_ATTACHED");}
  else if(strcmp(module_line,"OK")==0&&attached)next();
  return;
 }
 if(step==S_OPEN){if(sscanf(module_line,"+QMTOPEN: %d,%d",&id,&result)==2&&id==0){printf("CLOUD,OPEN,result=%d\n",result);if(result==0)next();else fail_link(now,"OPEN_FAILED");}return;}
 if(step==S_LOGIN){if(sscanf(module_line,"+QMTCONN: %d,%d,%d",&id,&result,&code)==3&&id==0){printf("CLOUD,LOGIN,result=%d,code=%d\n",result,code);if(result==0&&code==0){connected=1;link_fault=0;waiting=0;step=S_SUB_CMD;}else fail_link(now,"LOGIN_FAILED");}return;}
 if(step==S_SUB||step==S_SUB_REJECT||step==S_SUB_CMD){
  int fields=sscanf(module_line,"+QMTSUB: %d,%u,%d,%d",&id,&message,&result,&qos);
  if(fields>=3&&id==0&&message==(step==S_SUB?1u:step==S_SUB_REJECT?2u:3u)){
   if(result==0&&(fields<4||qos<=2)){
    if(step!=S_SUB_CMD)next();else{step=S_IDLE;waiting=0;subscription_at=now;publish_at=now+1000u;printf("CLOUD,READY,command_support=1\n");}
   }else fail_link(now,"SUBSCRIBE_FAILED");
  }return;
 }
 if(step==S_ACK){if(sscanf(module_line,"+QMTPUB: %d,%u,%d",&id,&message,&result)==3&&id==0&&message==packet_id){
  if(result==0){printf("CLOUD,PUBLISHED,id=%lu,fields=%u,group=%u\n",(unsigned long)sequence,packet_fields,group);if(tx_response){response_pending=0;tx_response=0;}else group=(group+1u)%5u;packet_id=packet_id==65535u?1u:packet_id+1u;step=S_IDLE;waiting=0;publish_at=now+REPORT_GAP_MS;}
  else fail_link(now,"PUBLISH_FAILED");}return;}
 if(strcmp(module_line,"OK")==0&&step<S_OPEN&&step!=S_CHECK_ATTACH)next();
}
static void append(const char *format,...)
{
 int n;va_list args;if(packet_failed)return;va_start(args,format);
 n=vsnprintf(payload+packet_used,sizeof(payload)-packet_used,format,args);va_end(args);
 if(n<0||(unsigned)n>=sizeof(payload)-packet_used){packet_failed=1;return;}packet_used+=(unsigned)n;
}
static void value(const char *name,long v)
{append("\"%s\":[{\"v\":%ld}],",name,v);packet_fields++;}
static void decimal(const char *name,int v,unsigned scale,unsigned digits)
{unsigned a=(unsigned)(v<0?-v:v);append("\"%s\":[{\"v\":%s%u.%0*u}],",name,v<0?"-":"",a/scale,(int)digits,a%scale);packet_fields++;}
static int packet(uint32_t now,const CaneState *c,const AgControl *ag)
{
 const CaneSample *s=&c->sample;const CaneProcess *p=&c->proc;const AgSample *a=&ag->sample;
 unsigned cv=now-s->at<=3000u?s->valid:0,av=now-a->at_ms<=5000u?a->valid:0;
 if(now-a->air_at_ms>5000u)av&=~AG_AIR;
 sequence=sequence>=2147483647u?1u:sequence+1u;packet_used=packet_fields=packet_failed=0;
 append("{\"id\":%lu,\"dp\":{",(unsigned long)sequence);
 /* 五组轮发，失效读数不造零；有效位随采样新鲜度更新。 */
 if(group==0){
  if(av&AG_AIR){decimal("air_temperature",a->temperature10,10,1);decimal("air_humidity",a->humidity10,10,1);}
  if(av&AG_SMOKE)value("smoke_relative",a->smoke);
  if(av&AG_LIGHT)value("light_relative",a->light);
  value("hopper_level",p->juice_level);
  value("environment_valid",av);value("environment_faults",ag->faults&~AG_FAULT_TIMING);
  /* 湿度报警单列一组：云端能直接按 limit 判断，不用自己解析故障位。 */
  value("humidity_alarm",ag->humi_alarm);value("humidity_limit",AG_HUMI_ALARM/10u);
 }else if(group==1){
  if(cv&CANE_WEIGHT)decimal("weight_kg",s->weight10,10000,4);
  if(cv&CANE_IR)decimal("object_temperature",s->object10,10,1);
  value("process_distance_mm",s->distance_mm);value("concentration_demo",p->conc);
  value("hall_adc",s->hall_adc);
  value("process_valid",cv);value("process_faults",c->faults);
 }else if(group==2){
  value("stage",p->stage);value("running",p->run);value("fill_count",p->fill_count);
  value("fill_total",p->fill_total);value("batch_count",p->batch);value("tare_pending",p->tare_wait);
  if(cv){value("fire_detected",s->fire);value("person_detected",s->person);}
  value("process_alarm",p->alarm);
 }else if(group==3){
  value("feeder_on",p->feeder);value("valve_open",p->valve);value("stir_setting",p->stir);
  value("process_fan_percent",c->fan);value("warehouse_fan_percent",ag->fan?80:0);value("lamp_on",ag->lamp);
 }else{
  decimal("weigh_target_kg",p->t_weigh10,10000,4);decimal("fill_target_kg",p->t_fill10,10000,4);
  decimal("boil_target_c",p->t_boil10,10,1);decimal("cool_target_c",p->t_cool10,10,1);
  value("concentration_limit",p->t_conc);value("uptime_s",now/1000u);
  append("\"firmware_version\":[{\"v\":\"cane-onenet-juice-8\"}],");packet_fields++;
 }
 value("sample_seq",sequence);
 if(packet_failed||!packet_used)return -1;
 payload[--packet_used]=0;append("}}");return packet_failed?-1:0;
}
void cane_wifi_poll(uint32_t now,const CaneState *c,const AgControl *ag)
{
 unsigned n;int length;
 current_ag=ag;
 if(executing){int r=cane_cloud_command(executing_kind,executing_values,0,ag);if(r!=10)make_response(r); }
 if(!initialized){
  board_uart(USART2,115200);NVIC_SetPriority(USART2_IRQn,6);NVIC_EnableIRQ(USART2_IRQn);USART_ITConfig(USART2,USART_IT_RXNE,ENABLE);
  NVIC_SetPriority(USART1_IRQn,6);NVIC_EnableIRQ(USART1_IRQn);USART_ITConfig(USART1,USART_IT_RXNE,ENABLE);
  initialized=1;printf("CLOUD,WAIT_CONFIG,uart=USART1,baud=115200\n");
 }
 while(config_rx.tail!=config_rx.head){
  uint8_t b=config_rx.bytes[config_rx.tail];config_rx.tail=(config_rx.tail+1u)&(RX_SIZE-1u);
  if(b=='\n'){if(!config_discard&&config_used){config_line[config_used]=0;configure(now);}config_used=config_discard=0;}
  else if(b!='\r'){if(config_used+1u<sizeof(config_line)&&!config_discard)config_line[config_used++]=(char)b;else{config_discard=1;wipe(config_line,sizeof(config_line));}}
 }
 while(module_rx.tail!=module_rx.head){
  uint8_t b=module_rx.bytes[module_rx.tail];module_rx.tail=(module_rx.tail+1u)&(RX_SIZE-1u);
  if(step==S_PUBLISH&&waiting&&b=='>'){step=S_DATA;waiting=0;line_used=0;continue;}
  if(b=='\n'){if(!line_discard&&line_used){module_line[line_used]=0;line_received(now);}line_used=line_discard=0;}
  else if(b!='\r'){if(line_used+1u<sizeof(module_line)&&!line_discard)module_line[line_used++]=(char)b;else{line_discard=1;line_used=0;rx_overflows++;}}
 }
 if(now-diagnostics_at>=10000u){diagnostics_at=now;
  printf("CLOUD,RX_DIAG,bytes=%lu,ore=%lu,errors=%lu,drops=%lu,lines=%lu,mqtt=%lu,overflow=%lu,pending=%lu,config_ore=%lu\n",
   (unsigned long)module_rx.received,(unsigned long)module_rx.overruns,(unsigned long)module_rx.errors,(unsigned long)module_rx.drops,
   (unsigned long)rx_lines,(unsigned long)rx_messages,(unsigned long)rx_overflows,(unsigned long)pending_report,(unsigned long)config_rx.overruns);
 }
 if(!configured||stopped||(int32_t)(now-retry_at)<0)return;
 if(waiting){if(now-sent_at>20000u){if(step==S_CLOSE||step==S_DISC){waiting=0;step=S_CLOSE==step?S_AT:S_CLOSE;}else fail_link(now,"TIMEOUT");}return;}
 if(step==S_IDLE){
  if(pending_report&&(uint32_t)(now-report_sent_at)>20000u){
   /* OneNET已在此前返回+QMTPUB:...,0，accepted主题是异步回执；回执缺失不能阻塞后续遥测。 */
   printf("CLOUD,REPORT_ACK_MISSING,id=%lu\n",(unsigned long)pending_report);
   pending_report=0;ack_timeouts++;
  }
  if(step==S_IDLE&&!response_pending&&((int32_t)(now-publish_at)<0||pending_report))return;
  if(step==S_IDLE){if(!response_pending&&packet(now,c,ag)){fail_link(now,"PACKET_OVERFLOW");return;}step=S_PUBLISH;}
 }
 command[0]=0;
 switch(step){
 case S_STATUS:strcpy(command,"AT+QMTCONN?\r\n");break;
 case S_DISC:strcpy(command,"AT+QMTDISC=0\r\n");break;
 case S_CLOSE:strcpy(command,"AT+QMTCLOSE=0\r\n");break;
 case S_AT:strcpy(command,"AT\r\n");break;
 case S_ECHO:strcpy(command,"ATE0\r\n");break;
 case S_WAKE:strcpy(command,"AT+QSCLK=0\r\n");break;
 case S_ATTACH:strcpy(command,"AT+CGATT=1\r\n");break;
 case S_CHECK_ATTACH:attached=0;strcpy(command,"AT+CGATT?\r\n");break;
 case S_VERSION:strcpy(command,"AT+QMTCFG=\"version\",0,4\r\n");break;
 case S_KEEPALIVE:strcpy(command,"AT+QMTCFG=\"keepalive\",0,120\r\n");break;
 case S_SESSION:strcpy(command,"AT+QMTCFG=\"session\",0,1\r\n");break;
 case S_LENGTH:strcpy(command,"AT+QMTPUB=?\r\n");break;
 case S_OPEN:snprintf(command,sizeof(command),"AT+QMTOPEN=0,\"%s\",%u\r\n",host,port);break;
 case S_LOGIN:snprintf(command,sizeof(command),"AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n",device,product,token);break;
 case S_SUB:snprintf(command,sizeof(command),"AT+QMTSUB=0,1,\"$sys/%s/%s/dp/post/json/accepted\",0\r\n",product,device);break;
 case S_SUB_REJECT:snprintf(command,sizeof(command),"AT+QMTSUB=0,2,\"$sys/%s/%s/dp/post/json/rejected\",0\r\n",product,device);break;
 case S_SUB_CMD:snprintf(command,sizeof(command),"AT+QMTSUB=0,3,\"$sys/%s/%s/cmd/request/+\",0\r\n",product,device);break;
 case S_PUBLISH:{
  unsigned i,at;
  /* 官方模块能力查询返回六参数格式，不包含消息长度参数。 */
  tx_response=response_pending;
  if(tx_response){strcpy(payload,response_payload);packet_used=(unsigned)strlen(payload);packet_fields=0;length=snprintf(command,sizeof(command),"AT+QMTPUB=0,%u,1,0,\"%s\",\"",packet_id,response_topic);}
  else length=snprintf(command,sizeof(command),"AT+QMTPUB=0,%u,1,0,\"$sys/%s/%s/dp/post/json\",\"",packet_id,product,device);
  if(length<0||(unsigned)length+2u*packet_used+4u>=sizeof(command)){fail_link(now,"COMMAND_OVERFLOW");return;}
  /* 官方仿真模块将最后一个消息参数原样转发，JSON不加AT层反斜杠。 */
  at=(unsigned)length;for(i=0;i<packet_used;i++)command[at++]=payload[i];
  command[at++]='"';command[at++]='\r';command[at++]='\n';command[at]=0;
  /* 诊断候选只订阅命令，遥测以QoS1模块完成与云端回读分别验收。 */
  if(!tx_response){pending_report=0;report_sent_at=now;}step=S_ACK;break;
 }
 case S_DATA:
  if(uart_send(payload,packet_used)){fail_link(now,"UART_TIMEOUT");return;}
  if(uart_send("\032",1)){fail_link(now,"UART_TIMEOUT");return;}
  step=S_ACK;waiting=1;sent_at=report_sent_at=now;return;
 default:return;
 }
 length=(int)strlen(command);n=step;waiting=1;sent_at=now;
 if(uart_send(command,(unsigned)length)){fail_link(now,"UART_TIMEOUT");return;}
 wipe(command,sizeof(command));printf("CLOUD,AT_SENT,step=%u\n",n);
}
