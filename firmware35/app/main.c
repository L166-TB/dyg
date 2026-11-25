/* 入口：外设初始化 → 1ms 时基 → 注册任务 → 进入周期调度。
   原工程由 LiteOS 的 main 完成这些，内核丢失后由这个文件接管。 */
#include "board_hal.h"
#include "los_task.h"
#include "app_runtime.h"

/* ---- newlib 系统调用桩 ----
   裸机上没有文件系统、没有进程，libc_nano 链接时会报 7 条
   "_close/_fstat/_isatty/_kill/_getpid/_lseek/_read is not implemented
    and will always fail"。平台要求 0 警告，这里把这 7 个符号定义成空实现，
   让 libc 的 _xxx_r 重入层直接调到本文件，警告消失。

   放在 main.c 而不是单独的 syscalls.c：main.c 一定参与链接，
   不会因为构建缓存没发现新文件而漏编（单独的 syscalls.c 就踩过这个坑）。
   _exit/_sbrk 不在这里定义 —— 启动文件和堆管理里已有实现，重复定义会冲突。

   这些函数只用来消警告，工程里没有文件读写、没有进程控制，
   printf 走的是串口重定向（见 board_hal.c）。 */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#undef errno
extern int errno;

int _close(int fd)
{
 (void)fd;
 errno = EBADF;
 return -1;
}

int _fstat(int fd, struct stat *st)
{
 (void)fd;
 if(st){
  st->st_mode = S_IFCHR;   /* 串口按字符设备上报，isatty 才认为它是终端 */
  st->st_size = 0;
 }
 return 0;
}

int _isatty(int fd)
{
 (void)fd;
 return 1;                /* 标准输入/输出都当终端，printf 才按行缓冲 */
}

int _kill(int pid, int sig)
{
 (void)pid; (void)sig;
 errno = EINVAL;
 return -1;
}

int _getpid(void)
{
 return 1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
 (void)fd; (void)offset; (void)whence;
 errno = ESPIPE;          /* 串口不可随机定位 */
 return (off_t)-1;
}

int _read(int fd, char *buf, int len)
{
 (void)fd; (void)buf; (void)len;
 errno = EBADF;
 return -1;
}

int main(void)
{
    board_hal_init();       /* GPIO/时钟/TIM5 微秒计时/USART1 日志/ADC */
    LOS_TickInit();         /* 1ms 节拍，LOS_TickCountGet 的来源 */
    app_start();            /* 队列、工序状态机、全部周期任务 */
    LOS_StartKernel();      /* 启动内核调度，不返回 */
    return 0;
}
