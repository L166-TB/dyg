/* 六道工序界面：每页对应一道工序的实际读数与可执行操作。 */
#ifndef CANE_GUI_H
#define CANE_GUI_H
#include "agri_control.h"
#include "cane.h"
#include <stdint.h>
enum {PAGE_WEIGH=1,PAGE_PURIFY,PAGE_BOIL,PAGE_FILL,PAGE_COOL,PAGE_STORE,PAGE_TARGET,PAGE_SAFETY,PAGE_COMMS};
/* PAGE_WEIGH..PAGE_STORE 是六道工序页，后面的都是功能页。 */
#define PAGE_LAST_FLOW PAGE_STORE
/* 日夜切换已从电路上删除：界面固定白底高对比单套配色，不再有 GUI_THEME。 */
enum {GUI_NONE=0,GUI_PREV=10,GUI_NEXT,GUI_START,GUI_STOP,GUI_STEP,GUI_WEIGH,GUI_TARE,GUI_SAFETY,GUI_COMMS,
 GUI_SPEAK=20,GUI_ACK,GUI_TARGET,GUI_ENTER,GUI_MINUS,GUI_PLUS,
 GUI_SELECT0=30,GUI_SELECT3=33};
void gui_init(void);
void gui_render(const AgControl *ag,const CaneState *c,uint32_t now);
void gui_invalidate(void);
int gui_touch_action(unsigned page,uint16_t x,uint16_t y);
#endif
