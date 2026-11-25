/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 任务管理接口，对标 OpenHarmony / LiteOS-M 的 kernel/include/los_task.h。
 *
 * 实现说明（重要，答辩时可直接引用）：
 *   本工程的"内核"是 app/los_kernel.c 里的一个 1ms 节拍驱动的协作式调度器，
 *   不是完整的 LiteOS-M 源码。之所以这么做，是因为平台工程只编 app/ 下的文件，
 *   放不下 OpenHarmony kernel/ 那套目录（约 80 个源文件）。
 *   因此这里只提供 LiteOS-M 的**接口形态**：
 *     · LOS_TaskCreate(name, *id, entry, arg, prio, stack, period_ms)
 *       —— 比 LiteOS-M 多一个 period_ms 参数，因为协程式内核收不回来栈帧，
 *          任务必须写成"查表 + 返回"的形式，不能在内部死循环；
 *     · 任务优先级沿用 LiteOS-M 约定：**数值越小优先级越高**（0 最高，31 最低），
 *          和 old code 里的 CMSIS 风格（数值大=优先级高）相反，调用处已同步调整。
 *   其余接口（队列/互斥/临界区/时基）的语义与 LiteOS-M 保持一致，
 *   应用层按 LiteOS-M 的写法调用即可。
 */
#ifndef _LOS_TASK_H
#define _LOS_TASK_H

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* 优先级范围，与 LiteOS-M 一致：0 最高、31 最低（本工程只用到 0~31 的低段） */
#define LOS_TASK_PRIORITY_HIGHEST  0
#define LOS_TASK_PRIORITY_LOWEST   31

/* 任务入口：参数只有一个 32 位 arg，与 LiteOS-M 的 TSK_ENTRY_FUNC 一致 */
typedef VOID (*TSK_ENTRY_FUNC)(UINT32 arg);

/* 任务创建参数，字段名对齐 LiteOS-M 的 TSK_INIT_PARAM_S。
   uwPeriodMs 是扩展字段：周期任务的调用间隔，0 表示每拍都跑。 */
typedef struct tagTskInitParam {
    CHAR           *pcName;         /* 任务名，最长 LOS_TASK_NAME_LEN-1 */
    TSK_ENTRY_FUNC  pfnTaskEntry;   /* 任务入口 */
    UINT16          usTaskPrio;     /* 优先级：数值越小越高 */
    UINT32          uwStackSize;    /* 栈大小（字节）。协作式内核不切栈，仅记录 */
    UINT32          uwArg;          /* 传给入口的参数 */
    UINT32          uwPeriodMs;     /* 扩展：周期节拍 */
} TSK_INIT_PARAM_S;

/* 任务运行信息，字段名对齐 LiteOS-M 的 TSK_INFO_S */
typedef struct {
    UINT32 uwTaskID;
    UINT32 uwPeakUsed;              /* 峰值栈用量；无 MMU 内核按 0 返回 */
    UINT32 uwStackSize;
    UINT8  bOvf;                    /* 栈溢出标志 */
} TSK_INFO_S;

#define LOS_TASK_NAME_LEN 16

/**
 * @brief 创建任务并加入调度表。
 * @param uwTaskID     [OUT] 任务 ID，后续 LOS_TaskInfoGet 用
 * @param initParam    [IN]  任务参数
 * @retval LOS_OK 成功；LOS_ERRNO_TSK_* 失败
 */
extern UINT32 LOS_TaskCreate(UINT32 *uwTaskID, TSK_INIT_PARAM_S *initParam);

/** 按 ID 取任务运行信息（栈用量、是否溢出）。 */
extern UINT32 LOS_TaskInfoGet(UINT32 taskId, TSK_INFO_S *taskInfo);

/** 挂起/恢复：协作式内核里只改一个标志位，被挂起的任务本拍跳过。 */
extern UINT32 LOS_TaskSuspend(UINT32 taskId);
extern UINT32 LOS_TaskResume(UINT32 taskId);

/** 当前任务 ID。超级循环里恒为调度器本身，返回 0。 */
extern UINT32 LOS_CurTaskIDGet(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* _LOS_TASK_H */
