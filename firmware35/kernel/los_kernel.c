/*
 * Copyright (c) 2026, 蔗糖制作系统——终版
 *
 * 轻量内核实现：1ms 节拍 + 优先级轮转的协作式调度器。
 * 接口形态对标 OpenHarmony / LiteOS-M，内核本身是本工程自带的精简实现。
 *
 * 为什么不是完整的 LiteOS-M：
 *   LiteOS-M 需要汇编上下文切换（los_dispatch.S）、任务栈独立分配、
 *   PendSV 中断里做栈帧搬运，整套下来 kernel/ 目录约 80 个源文件。
 *   本工程的平台只编译 app/ 下的文件，塞不下；而本系统真正用到内核的
 *   只有三件事——周期任务、定长队列、互斥量，所以这里按这三件事做一个
 *   同语义的最小实现，接口名、返回值、错误码全部对齐 LiteOS-M。
 *
 * 调度语义（与 LiteOS-M 的可用子集一致）：
 *   · 1ms SysTick 推进 tick，LOS_TickCountGet 直接返回；
 *   · 每个任务记录 period_ms 与 next_tick，到点就调用一次入口函数；
 *   · 任务按优先级顺序扫描（数值小=优先级高），高优先级任务在同一拍里先跑；
 *   · 入口函数必须"查表 + 返回"，不能在内部死循环——协程式内核不切栈，
 *     写不返回的入口会把整个调度器卡死；
 *   · 下一拍按「本拍起始 + period」排，任务自己花的时间自动从等待里扣掉，
 *     250ms 的采集节拍不会因为采集耗时被拖成 280ms。
 */
#include "los_task.h"
#include "los_queue.h"
#include "los_mux.h"
#include "los_tick.h"
#include "los_memory.h"
#include <string.h>

VOID *m_aucSysMem0 = (VOID *)0;

/* ======================== 时基 ======================== */

static volatile UINT32 s_tick;

UINT32 LOS_TickInit(VOID)
{
    /* 1ms 节拍：仿真器按 SysTick 推进，board_millis() 直接用这个计数 */
    SysTick->CTRL = 0u;
    SysTick->LOAD = (UINT32)(SystemCoreClock / 1000u) - 1u;
    SysTick->VAL  = 0u;
    SysTick->CTRL = 7u;                 /* CLKSOURCE=HCLK, TICKINT=1, ENABLE=1 */
    return LOS_OK;
}

VOID SysTick_Handler(VOID)
{
    s_tick++;
}

UINT64 LOS_TickCountGet(VOID)
{
    return (UINT64)s_tick;
}

VOID LOS_TaskDelay(UINT32 tick)
{
    UINT32 at = s_tick;
    if (!tick) return;
    /* 无符号差值等 tick：比 1ms 更细的等待没有意义，直接按毫秒推进 */
    while ((UINT32)(s_tick - at) < tick) { }
}

/* ======================== 临界区 ======================== */

UINT32 LOS_IntLock(VOID)
{
    UINT32 primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");
    return primask & 1u;
}

VOID LOS_IntRestore(UINT32 intSave)
{
    if (!(intSave & 1u)) {
        __asm volatile ("cpsie i" ::: "memory");
    }
}

VOID LOS_IntUnLock(VOID)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/* ======================== 队列 ======================== */

#define LOS_QUEUE_MAX   8
#define LOS_QUEUE_POOL  4096

typedef struct {
    UINT8  *buf;
    UINT16  cap;        /* 元素个数 */
    UINT16  item;       /* 单个元素字节数 */
    UINT16  head;
    UINT16  count;
    UINT8   used;
} LosQueue;

static LosQueue s_queues[LOS_QUEUE_MAX];
static UINT8    s_qpool[LOS_QUEUE_POOL];
static UINT32   s_qpoolAt;

UINT32 LOS_QueueCreate(const CHAR *queueName, UINT16 len, UINT32 *queueId,
                       UINT16 flags, UINT16 maxMsgSize)
{
    unsigned i;
    UINT32 need;
    (void)queueName;
    (void)flags;
    if (!queueId || !len || !maxMsgSize) return LOS_ERRNO_QUEUE_CREAT_PTR_NULL;
    for (i = 0; i < LOS_QUEUE_MAX; i++) {
        if (!s_queues[i].used) {
            need = (UINT32)len * maxMsgSize;
            need = (need + 3u) & ~3u;
            if (s_qpoolAt + need > LOS_QUEUE_POOL) return LOS_ERRNO_QUEUE_CREAT_PTR_NULL;
            s_queues[i].buf   = &s_qpool[s_qpoolAt];
            s_qpoolAt        += need;
            s_queues[i].cap   = len;
            s_queues[i].item  = maxMsgSize;
            s_queues[i].head  = 0;
            s_queues[i].count = 0;
            s_queues[i].used  = 1;
            memset(s_queues[i].buf, 0, need);
            *queueId = i + 1u;
            return LOS_OK;
        }
    }
    return LOS_ERRNO_QUEUE_CREAT_PTR_NULL;
}

UINT32 LOS_QueueWriteCopy(UINT32 queueId, VOID *bufferAddr, UINT32 bufferSize, UINT32 timeOut)
{
    LosQueue *q;
    UINT16 tail;
    UINT32 irq;
    (void)timeOut;
    if (queueId == 0u || queueId > LOS_QUEUE_MAX || !bufferAddr) return LOS_ERRNO_QUEUE_INVALID;
    q = &s_queues[queueId - 1u];
    if (!q->used || bufferSize > q->item) return LOS_ERRNO_QUEUE_INVALID;
    irq = LOS_IntLock();
    if (q->count >= q->cap) {
        LOS_IntRestore(irq);
        return LOS_ERRNO_QUEUE_ISFULL;          /* 调用方本来就用 NO_WAIT，按丢包处理 */
    }
    tail = (UINT16)((q->head + q->count) % q->cap);
    memcpy(q->buf + (UINT32)tail * q->item, bufferAddr, bufferSize);
    if (bufferSize < q->item) {
        memset(q->buf + (UINT32)tail * q->item + bufferSize, 0, q->item - bufferSize);
    }
    q->count++;
    LOS_IntRestore(irq);
    return LOS_OK;
}

UINT32 LOS_QueueReadCopy(UINT32 queueId, VOID *bufferAddr, UINT32 *bufferSize, UINT32 timeOut)
{
    LosQueue *q;
    UINT32 irq;
    (void)timeOut;
    if (queueId == 0u || queueId > LOS_QUEUE_MAX || !bufferAddr) return LOS_ERRNO_QUEUE_INVALID;
    q = &s_queues[queueId - 1u];
    if (!q->used) return LOS_ERRNO_QUEUE_INVALID;
    irq = LOS_IntLock();
    if (!q->count) {
        LOS_IntRestore(irq);
        return LOS_ERRNO_QUEUE_ISEMPTY;
    }
    memcpy(bufferAddr, q->buf + (UINT32)q->head * q->item, q->item);
    if (bufferSize && *bufferSize < q->item) {
        LOS_IntRestore(irq);
        return LOS_ERRNO_QUEUE_INVALID;
    }
    if (bufferSize) *bufferSize = q->item;
    q->head = (UINT16)((q->head + 1u) % q->cap);
    q->count--;
    LOS_IntRestore(irq);
    return LOS_OK;
}

UINT32 LOS_QueueDelete(UINT32 queueId)
{
    if (queueId == 0u || queueId > LOS_QUEUE_MAX) return LOS_ERRNO_QUEUE_INVALID;
    if (!s_queues[queueId - 1u].used) return LOS_ERRNO_QUEUE_INVALID;
    s_queues[queueId - 1u].used  = 0;
    s_queues[queueId - 1u].count = 0;
    /* 池空间不回收：删除只发生在退出阶段，回收会破坏别的队列指针 */
    return LOS_OK;
}

/* ======================== 互斥量 ======================== */

#define LOS_MUX_MAX 4

typedef struct {
    UINT8  used;
    UINT8  locked;
    UINT32 owner;       /* 持有者任务 ID，0 = 无人持有 */
} LosMux;

static LosMux s_mux[LOS_MUX_MAX];

UINT32 LOS_MuxCreate(UINT32 *muxId)
{
    unsigned i;
    if (!muxId) return LOS_ERRNO_MUX_PTR_NULL;
    for (i = 0; i < LOS_MUX_MAX; i++) {
        if (!s_mux[i].used) {
            s_mux[i].used   = 1;
            s_mux[i].locked = 0;
            s_mux[i].owner  = 0;
            *muxId = i + 1u;
            return LOS_OK;
        }
    }
    return LOS_ERRNO_MUX_PTR_NULL;
}

UINT32 LOS_MuxPend(UINT32 muxId, UINT32 timeOut)
{
    (void)timeOut;
    if (muxId == 0u || muxId > LOS_MUX_MAX || !s_mux[muxId - 1u].used) {
        return LOS_ERRNO_MUX_INVALID;
    }
    if (s_mux[muxId - 1u].locked) {
        /* 单线程协作式内核不会在任务中途被切走，走到这里说明有嵌套 Pend 或逻辑错误。
           返回错误而不是死等——死等会把整个调度器卡住，屏幕上什么都看不到。 */
        return LOS_ERRNO_MUX_PENDED;
    }
    s_mux[muxId - 1u].locked = 1;
    s_mux[muxId - 1u].owner  = LOS_CurTaskIDGet();
    return LOS_OK;
}

UINT32 LOS_MuxPost(UINT32 muxId)
{
    if (muxId == 0u || muxId > LOS_MUX_MAX || !s_mux[muxId - 1u].used) {
        return LOS_ERRNO_MUX_INVALID;
    }
    if (!s_mux[muxId - 1u].locked) return LOS_ERRNO_MUX_INVALID;
    s_mux[muxId - 1u].locked = 0;
    s_mux[muxId - 1u].owner  = 0;
    return LOS_OK;
}

UINT32 LOS_MuxDelete(UINT32 muxId)
{
    if (muxId == 0u || muxId > LOS_MUX_MAX) return LOS_ERRNO_MUX_INVALID;
    s_mux[muxId - 1u].used   = 0;
    s_mux[muxId - 1u].locked = 0;
    s_mux[muxId - 1u].owner  = 0;
    return LOS_OK;
}

/* ======================== 内存 ======================== */

UINT32 LOS_MemTotalUsedGet(VOID *pool)
{
    (void)pool;
    return 0u;                          /* 无常驻内核堆，日志里这一列恒为 0 */
}

/* ======================== 任务 ======================== */

#define LOS_TASK_MAX 8

typedef struct {
    CHAR    *name;
    TSK_ENTRY_FUNC entry;
    UINT32   arg;
    UINT32   period;        /* ms */
    UINT32   next;          /* 下一次调用的 tick */
    UINT32   stackSize;
    UINT16   prio;          /* 数值小 = 优先级高 */
    UINT8    used;
    UINT8    suspended;
} LosTask;

static LosTask s_tasks[LOS_TASK_MAX];
static unsigned s_taskNum;
static UINT32 s_curTaskId;

UINT32 LOS_TaskCreate(UINT32 *uwTaskID, TSK_INIT_PARAM_S *initParam)
{
    if (!uwTaskID || !initParam) return LOS_ERRNO_TSK_NO_MEMORY;
    if (!initParam->pfnTaskEntry) return LOS_ERRNO_TSK_ENTRY_NULL;
    if (!initParam->pcName) return LOS_ERRNO_TSK_NAME_INVALID;
    if (s_taskNum >= LOS_TASK_MAX) return LOS_ERRNO_TSK_NO_MEMORY;

    s_tasks[s_taskNum].name      = initParam->pcName;
    s_tasks[s_taskNum].entry     = initParam->pfnTaskEntry;
    s_tasks[s_taskNum].arg       = initParam->uwArg;
    s_tasks[s_taskNum].period    = initParam->uwPeriodMs ? initParam->uwPeriodMs : 1u;
    s_tasks[s_taskNum].next      = 0u;
    s_tasks[s_taskNum].stackSize = initParam->uwStackSize;
    s_tasks[s_taskNum].prio      = initParam->usTaskPrio;
    s_tasks[s_taskNum].used      = 1;
    s_tasks[s_taskNum].suspended = 0;
    s_taskNum++;
    *uwTaskID = s_taskNum;              /* ID 从 1 开始，0 表示"无效" */
    return LOS_OK;
}

UINT32 LOS_TaskInfoGet(UINT32 taskId, TSK_INFO_S *taskInfo)
{
    if (!taskInfo || taskId == 0u || taskId > s_taskNum) return LOS_ERRNO_TSK_ID_INVALID;
    taskInfo->uwTaskID  = taskId;
    taskInfo->uwPeakUsed = 0u;          /* 不切栈，没有峰值统计 */
    taskInfo->uwStackSize = s_tasks[taskId - 1u].stackSize;
    taskInfo->bOvf = 0u;
    return LOS_OK;
}

UINT32 LOS_TaskSuspend(UINT32 taskId)
{
    if (taskId == 0u || taskId > s_taskNum) return LOS_ERRNO_TSK_ID_INVALID;
    s_tasks[taskId - 1u].suspended = 1;
    return LOS_OK;
}

UINT32 LOS_TaskResume(UINT32 taskId)
{
    if (taskId == 0u || taskId > s_taskNum) return LOS_ERRNO_TSK_ID_INVALID;
    s_tasks[taskId - 1u].suspended = 0;
    s_tasks[taskId - 1u].next = s_tick;
    return LOS_OK;
}

UINT32 LOS_CurTaskIDGet(VOID)
{
    return s_curTaskId;
}

/* ======================== 调度 ======================== */

VOID LOS_StartKernel(VOID)
{
    UINT32 now, i, j, last, spin = 0u, dead = 0u;
    LosTask tmp;

    /* 建立任务表时按优先级插入排序（数值小=优先级高），
       这样扫描时天然"高优先级先跑"，不需要每拍再比较。
       排序只做一次，比每拍比较省几个周期，也让日志顺序稳定。 */
    for (i = 1u; i < s_taskNum; i++) {
        tmp = s_tasks[i];
        j = i;
        while (j > 0u && s_tasks[j - 1u].prio > tmp.prio) {
            s_tasks[j] = s_tasks[j - 1u];
            j--;
        }
        s_tasks[j] = tmp;
    }
    for (i = 0; i < s_taskNum; i++) s_tasks[i].next = s_tick;
    last = s_tick;

    for (;;) {
        now = s_tick;

        /* 节拍守护：仿真器里 SysTick 中断没打通时，s_tick 会一直不动，
           表现为「程序已运行但屏幕/串口什么都不刷新」。这里先试着重新
           使能 SysTick，连续多轮仍然不动就用软件补一拍，保证退化运行。 */
        if (now == last) {
            if (++spin >= 50000u) {
                spin = 0u;
                if (!(SysTick->CTRL & 1u)) SysTick->CTRL = 7u;
                if (++dead >= 8u) { dead = 0u; s_tick++; }
            }
        } else {
            last = now;
            spin = 0u;
            dead = 0u;
        }

        for (i = 0; i < s_taskNum; i++) {
            if (!s_tasks[i].used) continue;
            if (s_tasks[i].suspended) continue;
            if ((INT32)(now - s_tasks[i].next) < 0) continue;
            s_curTaskId = i + 1u;
            s_tasks[i].entry(s_tasks[i].arg);
            /* 下一拍按「本拍起始 + period」排：entry 自己消耗的时间自动被扣掉，
               采集/显示忙的时候也不会把节拍越拖越长。 */
            s_tasks[i].next += s_tasks[i].period;
            now = s_tick;
            if ((INT32)(now - s_tasks[i].next) >= 0) {
                s_tasks[i].next = now + s_tasks[i].period;   /* 落后太多就重新对齐 */
            }
        }
        s_curTaskId = 0u;
    }
}
