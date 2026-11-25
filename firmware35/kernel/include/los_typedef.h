/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 基础类型与系统常量，对标 OpenHarmony / LiteOS-M 的 kernel/include/los_typedef.h。
 * 类型名（UINT32/INT32/VOID/CHAR…）与 LiteOS-M 完全一致，避免应用层改写。
 */
#ifndef _LOS_TYPEDEF_H
#define _LOS_TYPEDEF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef signed char        INT8;
typedef signed short       INT16;
typedef signed int         INT32;
typedef signed long long   INT64;
typedef char               CHAR;
typedef void               VOID;
typedef void              *VOID_PTR;
typedef UINT32             BOOL;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* ---- 通用错误码，编号沿用 LiteOS-M，便于和真实内核日志对照 ---- */
#define LOS_OK                  0u
#define LOS_FAIL                1u

#define LOS_ERRNO_TSK_NO_MEMORY   0x02000201u   /* 任务表已满 */
#define LOS_ERRNO_TSK_ID_INVALID  0x02000209u   /* 任务 ID 非法 */
#define LOS_ERRNO_TSK_NAME_INVALID 0x0200020cu /* 任务名为空 */
#define LOS_ERRNO_TSK_ENTRY_NULL  0x0200020bu   /* 入口为空 */

#define LOS_ERRNO_QUEUE_CREAT_PTR_NULL   0x02000601u
#define LOS_ERRNO_QUEUE_INVALID          0x02000609u
#define LOS_ERRNO_QUEUE_ISFULL           0x02000626u
#define LOS_ERRNO_QUEUE_ISEMPTY          0x02000627u

#define LOS_ERRNO_MUX_PTR_NULL   0x02000501u
#define LOS_ERRNO_MUX_INVALID    0x02000506u
#define LOS_ERRNO_MUX_PENDED     0x02000510u   /* 已持有还来 Pend */

/* ---- 等待时间，单位 ms ---- */
#define LOS_WAIT_FOREVER  0xFFFFFFFFu
#define LOS_NO_WAIT       0u

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* _LOS_TYPEDEF_H */
