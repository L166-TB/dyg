/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 队列接口，对标 OpenHarmony / LiteOS-M 的 kernel/include/los_queue.h。
 * 语义与 LiteOS-M 一致：定长元素、环形缓冲、WriteCopy/ReadCopy 拷贝进出。
 * 本工程所有调用方都用 LOS_NO_WAIT，所以实现里不阻塞；超时参数保留只为接口兼容。
 */
#ifndef _LOS_QUEUE_H
#define _LOS_QUEUE_H

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @brief 创建队列。
 * @param queueName  [IN]  队列名（仅日志用）
 * @param len        [IN]  元素个数
 * @param queueId    [OUT] 队列句柄
 * @param flags      [IN]  保留，传 0
 * @param maxMsgSize [IN]  单个元素字节数
 */
extern UINT32 LOS_QueueCreate(const CHAR *queueName, UINT16 len, UINT32 *queueId,
                              UINT16 flags, UINT16 maxMsgSize);

/** 拷入一条消息。队列满返回 LOS_ERRNO_QUEUE_ISFULL，调用方按丢包处理。 */
extern UINT32 LOS_QueueWriteCopy(UINT32 queueId, VOID *bufferAddr, UINT32 bufferSize,
                                 UINT32 timeOut);

/** 拷出一条消息。队列空返回 LOS_ERRNO_QUEUE_ISEMPTY，bufferSize 回写实际长度。 */
extern UINT32 LOS_QueueReadCopy(UINT32 queueId, VOID *bufferAddr, UINT32 *bufferSize,
                                UINT32 timeOut);

/** 删除队列，释放占用的池空间（本工程在运行期不用，留着接口完整）。 */
extern UINT32 LOS_QueueDelete(UINT32 queueId);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* _LOS_QUEUE_H */
