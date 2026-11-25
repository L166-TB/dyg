#include "cane_cloud_json.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
static void space(const char **p){while(**p&&strchr(" \r\n\t",**p))(*p)++;}
static int take(const char **p,char c){space(p);if(**p!=c)return 0;(*p)++;return 1;}
static int word(const char **p,char out[32])
{unsigned n=0;if(!take(p,'"'))return 0;while(**p&&**p!='"'){if(n>=31||!(isalnum((unsigned char)**p)||**p=='_'))return 0;out[n++]=*(*p)++;}out[n]=0;return take(p,'"');}
static int number(const char **p,int32_t *v,unsigned scale)
{uint32_t whole=0,frac=0,places=1;unsigned count=0;space(p);if(!isdigit((unsigned char)**p))return 0;if(**p=='0'&&isdigit((unsigned char)(*p)[1]))return 0;while(isdigit((unsigned char)**p)){if(++count>6)return 0;whole=whole*10u+(unsigned)(*(*p)++-'0');}if(**p=='.'){(*p)++;count=0;while(isdigit((unsigned char)**p)){if(++count>4)return 0;frac=frac*10u+(unsigned)(*(*p)++-'0');places*=10u;}if(!count)return 0;}if(whole>30000u||((frac*scale)%places))return 0;*v=(int32_t)(whole*scale+frac*scale/places);return 1;}
int cane_cloud_parse(const char *json,unsigned *kind,int32_t values[4])
{
 const char *p=json;char key[32],name[32]="",keys[5][32];int32_t raw[5];unsigned fields=0,seen=0,i,j;
 if(!take(&p,'{'))return 0;
 for(;;){if(!word(&p,key)||!take(&p,':'))return 0;
  if(!strcmp(key,"command")){if(seen&1u||!word(&p,name))return 0;seen|=1u;}
  else if(!strcmp(key,"params")){if(seen&2u||!take(&p,'{'))return 0;seen|=2u;space(&p);if(*p!='}')for(;;){if(fields>=5||!word(&p,keys[fields])||!take(&p,':')||!number(&p,&raw[fields],10000u))return 0;for(j=0;j<fields;j++)if(!strcmp(keys[j],keys[fields]))return 0;fields++;space(&p);if(*p!=',')break;p++;}if(!take(&p,'}'))return 0;}
  else return 0;
  space(&p);if(*p!=',')break;p++;
 }
 if(!take(&p,'}'))return 0;
 space(&p);if(*p||!(seen&1u))return 0;
 if(!strcmp(name,"set_run")){*kind=0;if(fields!=1||strcmp(keys[0],"running")||raw[0]%10000||raw[0]>10000)return 0;values[0]=raw[0]/10000;}
 else if(!strcmp(name,"set_targets")){static const char *const names[]={"weigh_target_kg","fill_target_kg","boil_target_c","cool_target_c"};*kind=1;if(fields!=4)return 0;for(i=0;i<4;i++){for(j=0;j<4;j++)if(!strcmp(keys[j],names[i]))break;if(j==4)return 0;if(i<2)values[i]=raw[j];else{if(raw[j]%10000)return 0;values[i]=raw[j]/1000;}}}
 else if(!strcmp(name,"tare")){*kind=2;if(fields)return 0;}
 else if(!strcmp(name,"ack_alarm")){*kind=3;if(fields)return 0;}
 else if(!strcmp(name,"next_stage")){*kind=4;if(fields!=1||strcmp(keys[0],"expected_stage")||raw[0]%10000||raw[0]<10000||raw[0]>60000)return 0;values[0]=raw[0]/10000;}
 else return 0;
 return 1;
}
