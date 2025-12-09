/*
 * user.h - 用户态系统调用接口
 *
 * 用户程序通过这些函数调用系统调用
 */

#ifndef USER_H
#define USER_H

#include "types.h"

/* ============== 进程控制类 ============== */

/* 创建子进程 */
int fork(void);

/* 终止当前进程 */
void exit(int status) __attribute__((noreturn));

/* 等待子进程结束 */
int wait(int *status);

/* 终止指定进程 */
int kill(int pid);

/* 获取当前进程PID */
int getpid(void);

/* 调整堆大小 */
void* sbrk(int n);

/* 进程睡眠 */
int sleep(int n);

/* 获取系统运行时间 */
int uptime(void);

/* ============== 内存管理类 ============== */

/* 内存映射 */
void* mmap(void *addr, uint64 length, int prot, int flags, int fd, uint64 offset);

/* 解除内存映射 */
int munmap(void *addr, uint64 length);

/* mmap 保护标志 */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

/* mmap 映射标志 */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20

/* mmap 错误返回值 */
#define MAP_FAILED      ((void*)-1)

/* ============== 文件操作类 ============== */

/* 从文件读取 */
int read(int fd, void *buf, int count);

/* 写入文件 */
int write(int fd, const void *buf, int count);

/* 打开文件 */
int open(const char *path, int flags);

/* 关闭文件 */
int close(int fd);

/* ============== 文件打开标志 ============== */

#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_CREATE    0x200
#define O_TRUNC     0x400

/* ============== 标准文件描述符 ============== */

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

#endif /* USER_H */
