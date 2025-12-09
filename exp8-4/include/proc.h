/*
 * proc.h - 进程管理
 */

#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "trap.h"

/* ============== 常量 ============== */

/* VMA 参数 */
#define NVMA            16  /* 每进程最大 VMA 数量 */
#define PGSIZE          4096

#define NPROC       64
#define KSTACK_SIZE 4096

/* 调度算法 */
#define SCHED_RR        0   /* 轮转调度 */
#define SCHED_PRIO      1   /* 优先级调度 */
#define SCHED_MLFQ      2   /* 多级反馈队列 */

/* 优先级 */
#define PRIO_LEVELS     4   /* 优先级级数 */
#define PRIO_HIGH       0
#define PRIO_NORMAL     1
#define PRIO_LOW        2
#define PRIO_IDLE       3
#define PRIO_DEFAULT    PRIO_NORMAL

/* MLFQ 参数 */
#define MLFQ_LEVELS     3   /* 队列级数 */
#define MLFQ_BOOST_INTERVAL 50  /* 优先级提升间隔 (ticks) */

/* ============== 数据结构 ============== */

/* 虚拟内存区域 (VMA) */
struct vma {
    uint64 addr;            /* 起始地址 */
    uint64 length;          /* 映射长度 */
    int prot;               /* 保护标志 */
    int flags;              /* 映射标志 */
    int fd;                 /* 文件描述符 */
    uint64 offset;          /* 文件偏移 */
    int used;               /* 是否使用 */
};

enum procstate { UNUSED, USED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

/* 调度上下文 (callee-saved 寄存器) */
struct context {
    uint64 ra, sp;
    uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

/* 进程控制块 */
struct proc {
    volatile int lock;
    enum procstate state;
    int pid;
    char name[16];
    void *chan;
    int killed;
    int xstate;
    struct proc *parent;
    uint64 kstack;
    struct context context;
    void (*entry)(void);
    uint64 sched_count;
    uint64 total_ticks;
    uint64 start_tick;
    /* 调度相关 */
    int priority;           /* 静态优先级 */
    int queue_level;        /* MLFQ 队列级别 */
    int time_slice;         /* 剩余时间片 */
    uint64 wait_start;      /* 开始等待时间 */
    /* 系统调用相关 */
    struct trapframe *trapframe;  /* 陷阱帧指针 */
    uint64 sz;                    /* 进程内存大小 */
    /* 内存映射 */
    struct vma vmas[NVMA];        /* 虚拟内存区域数组 */
};

/* CPU 状态 */
struct cpu {
    struct proc *proc;
    struct context context;
    int noff;
    int intena;
};

/* 调度统计 */
struct sched_stats {
    uint64 total_switches;      /* 总切换次数 */
    uint64 total_wait_time;     /* 总等待时间 */
    uint64 total_turnaround;    /* 总周转时间 */
    int completed_procs;        /* 完成进程数 */
};

/* ============== 接口 ============== */

/* 进程管理 */
void proc_init(void);
int  create_process(void (*entry)(void), const char *name);
void exit_process(int status);
int  wait_process(int *status);
struct proc* myproc(void);
struct cpu*  mycpu(void);

/* 系统调用支持 */
int  fork(void);
int  kill(int pid);

/* 调度器 */
void scheduler(void);
void yield(void);
void sched(void);
void set_scheduler(int type);
int  get_scheduler(void);
const char* scheduler_name(int type);

/* 优先级 */
void set_priority(struct proc *p, int prio);
int  get_priority(struct proc *p);
int  create_process_prio(void (*entry)(void), const char *name, int prio);

/* 调度统计 */
void reset_sched_stats(void);
struct sched_stats get_sched_stats(void);

/* 同步原语 */
void sleep(void *chan);
void wakeup(void *chan);

/* 上下文切换 */
void swtch(struct context *old, struct context *new);

/* 中断管理 */
void push_off(void);
void pop_off(void);

/* 调试 */
void debug_proc_table(void);

/* ============== 全局变量 ============== */

extern struct proc proc[NPROC];
extern struct cpu cpus[1];
extern struct proc *initproc;

#endif
