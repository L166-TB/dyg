/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 内存接口，对标 OpenHarmony / LiteOS-M 的 kernel/include/los_memory.h。
 * 本工程没有内核堆（队列池和任务表都是静态数组），
 * LOS_MemTotalUsedGet 因此恒返回 0——日志里那一列就是 0，
 * 不代表内存泄露，而是"这套内核不动态分配"。
 */
#ifndef _LOS_MEMORY_H
#define _LOS_MEMORY_H

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/** 系统内存池指针。LiteOS-M 里是实际堆地址，这里是一个占位。 */
extern VOID *m_aucSysMem0;

/** 已用堆字节数。无常驻堆，返回 0。 */
extern UINT32 LOS_MemTotalUsedGet(VOID *pool);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* _LOS_MEMORY_H */
