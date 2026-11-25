/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 互斥量接口，对标 OpenHarmony / LiteOS-M 的 kernel/include/los_mux.h。
 *
 * 与 LiteOS-M 的差别（必须说明，否则会误以为有优先级继承）：
 *   本工程是单线程协作式调度——同一时刻只有一个任务在跑，任务切换只发生在
 *   调度器的任务边界上。所以互斥量退化为"计数 + 持有者记录"，
 *   Pend 在已被占用时返回 LOS_ERRNO_MUX_PENDED，不会阻塞等待，
 *   也没有优先级继承。这与 LiteOS-M 的 `LOS_MuxPend(muxId, LOS_WAIT_FOREVER)`
 *   在无竞争场景下的行为等价。
 */
#ifndef _LOS_MUX_H
#define _LOS_MUX_H

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/** 创建互斥量。muxId 从 1 开始编号，0 保留给"创建失败"。 */
extern UINT32 LOS_MuxCreate(UINT32 *muxId);

/** 申请互斥量。已被他人持有则返回 LOS_ERRNO_MUX_PENDED。 */
extern UINT32 LOS_MuxPend(UINT32 muxId, UINT32 timeOut);

/** 释放互斥量。非持有者释放返回 LOS_ERRNO_MUX_INVALID。 */
extern UINT32 LOS_MuxPost(UINT32 muxId);

/** 删除互斥量。 */
extern UINT32 LOS_MuxDelete(UINT32 muxId);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* _LOS_MUX_H */
