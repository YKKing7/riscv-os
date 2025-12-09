/*
 * syscall.h - 系统调用接口定义
 * 
 * 功能：
 *   - 系统调用号定义
 *   - 参数提取接口 (argint/argaddr/argstr)
 *   - 用户内存访问 (copyin/copyout)
 *   - mmap/munmap 常量定义
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "trap.h"

/* ============== 系统调用号定义 ============== */

/* 进程控制类 */
#define SYS_fork        1   /* 创建子进程 */
#define SYS_exit        2   /* 终止进程 */
#define SYS_wait        3   /* 等待子进程 */
#define SYS_kill        4   /* 终止指定进程 */
#define SYS_getpid      5   /* 获取进程ID */
#define SYS_sbrk        6   /* 调整堆大小 */
#define SYS_sleep       11  /* 进程睡眠 */
#define SYS_uptime      12  /* 获取系统运行时间 */

/* 文件操作类 */
#define SYS_read        7   /* 读文件 */
#define SYS_write       8   /* 写文件 */
#define SYS_open        9   /* 打开文件 */
#define SYS_close       10  /* 关闭文件 */

/* 内存管理类 */
#define SYS_mmap        13  /* 内存映射 */
#define SYS_munmap      14  /* 解除映射 */

/* 性能优化类 */
#define SYS_getpid_fast 15  /* 快速 getpid */
#define SYS_batch       16  /* 批量系统调用 */

#define NSYSCALL        17  /* 系统调用总数 */
#define BATCH_MAX       8   /* 批量调用最大数量 */

/* ============== mmap 常量定义 ============== */

/* 内存保护标志 */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

/* 映射类型标志 */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20

#define MAP_FAILED      ((void*)-1)

/* ============== 系统调用接口 ============== */

void syscall_init(void);                            /* 初始化 */
void syscall(void);                                 /* 分发入口 */

/* 参数提取 */
int argint(int n, int *ip);                         /* 整数参数 */
int argaddr(int n, uint64 *ip);                     /* 地址参数 */
int argstr(int n, char *buf, int max);              /* 字符串参数 */

/* 用户内存访问 */
int copyin(uint64 dst, uint64 src, uint64 len);     /* 用户->内核 */
int copyout(uint64 dst, uint64 src, uint64 len);    /* 内核->用户 */
int copyinstr(char *dst, uint64 src, uint64 max);   /* 拷贝字符串 */
int check_user_ptr(const void *ptr, int size);      /* 指针验证 */

/* 调试支持 */
void enable_syscall_trace(int enable);
int get_syscall_trace(void);

/* ============== 系统调用实现声明 ============== */

/* 进程控制类 - sysproc.c */
int sys_fork(void);
int sys_exit(void);
int sys_wait(void);
int sys_kill(void);
int sys_getpid(void);
int sys_sbrk(void);
int sys_sleep(void);
int sys_uptime(void);

/* 内存管理类 - sysproc.c */
uint64 sys_mmap(void);
int sys_munmap(void);

/* 性能优化类 - sysproc.c */
int sys_getpid_fast(void);
int sys_batch(void);

/* 文件操作类 - sysfile.c */
int sys_read(void);
int sys_write(void);
int sys_open(void);
int sys_close(void);

#endif /* SYSCALL_H */
