/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 时基与中断接口，对标 OpenHarmony / LiteOS-M 的 los_tick.h 与 los_interrupt.h。
 * 本工程把两个头文件合成一个，因为都只涉及 SysTick 与 PRIMASK 两件事。
 */
#ifndef _LOS_TICK_H
#define _LOS_TICK_H

#include "los_typedef.h"
#include "stm32f4xx.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ---- 时基 ---- */

/** 初始化 1ms 节拍（SysTick），对应 LiteOS-M 的 LOS_TickInit。 */
extern UINT32 LOS_TickInit(VOID);

/** 当前 tick 数（1 tick = 1ms）。 */
extern UINT64 LOS_TickCountGet(VOID);

/** 忙等 ms 毫秒。用于初始化阶段和几十毫秒级的器件等待。 */
extern VOID LOS_TaskDelay(UINT32 tick);

/* ---- 中断 / 临界区 ---- */

/**
 * @brief 关中断并返回原 PRIMASK，与 LiteOS-M 的 LOS_IntLock 一致。
 * @note  返回的是可嵌套的状态字，必须原样传给 LOS_IntRestore。
 */
extern UINT32 LOS_IntLock(VOID);

/** 按 LOS_IntLock 返回的状态字恢复中断。 */
extern VOID LOS_IntRestore(UINT32 intSave);

/** 进/出临界区（可嵌套），语义同 LiteOS-M：内部维护嵌套计数，最外层才真正关中断。 */
extern VOID LOS_IntUnLock(VOID);

/* ---- 调度器入口（本工程扩展，LiteOS-M 里由 main 后的 rtos 启动代替） ---- */

/** 启动调度器，不返回。 */
extern VOID LOS_StartKernel(VOID) __attribute__((noreturn));

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* _LOS_TICK_H */
