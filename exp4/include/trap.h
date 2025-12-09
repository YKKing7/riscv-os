/*
 * trap.h - 中断与异常处理接口
 */

#ifndef TRAP_H
#define TRAP_H

#include "types.h"
#include "riscv.h"

/* ============== 陷阱帧 ============== */

struct trapframe {
    /* 通用寄存器 x1-x31 */
    uint64 ra, sp, gp, tp;
    uint64 t0, t1, t2;
    uint64 s0, s1;
    uint64 a0, a1, a2, a3, a4, a5, a6, a7;
    uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint64 t3, t4, t5, t6;
    /* CSR */
    uint64 sepc, sstatus, scause, stval;
};

/* ============== 函数类型 ============== */

typedef void (*interrupt_handler_t)(void);
typedef void (*exception_handler_t)(struct trapframe *tf);
typedef int (*shared_handler_t)(void *dev_id);  /* 返回1表示已处理 */

/* ============== 优先级 ============== */

#define IRQ_PRIO_HIGHEST    0
#define IRQ_PRIO_HIGH       1
#define IRQ_PRIO_NORMAL     2   /* 默认 */
#define IRQ_PRIO_LOW        3
#define IRQ_PRIO_LOWEST     4
#define IRQ_PRIO_LEVELS     5

/* ============== 初始化 ============== */

void trap_init(void);
void trap_inithart(void);

/* ============== 中断注册 ============== */

void register_interrupt_handler(int irq, interrupt_handler_t handler);
void register_interrupt_handler_prio(int irq, interrupt_handler_t handler, int prio);
void register_exception_handler(int exc, exception_handler_t handler);
void enable_interrupt(int irq);
void disable_interrupt(int irq);

/* ============== 共享中断 ============== */

int request_shared_irq(int irq, shared_handler_t handler, void *dev_id, const char *name);
void free_shared_irq(int irq, void *dev_id);

/* ============== 优先级与嵌套 ============== */

void set_irq_priority(int irq, int priority);
int get_irq_priority(int irq);
int get_irq_nesting_level(void);
int get_current_irq_priority(void);

/* ============== 核心处理 ============== */

void kerneltrap(void);
int devintr(void);

/* ============== 时钟 ============== */

#define TIMER_INTERVAL  10000000  /* ~0.1s @100MHz */

uint64 get_time(void);
uint64 get_ticks(void);
void timer_interrupt(void);
void set_next_timer(void);

/* ============== 统计 ============== */

struct irq_stats {
    uint64 count, total_cycles, max_cycles;
    int priority, nested_count;
};

void get_irq_stats(int irq, struct irq_stats *stats);
void print_irq_stats(void);

/* ============== 外部符号 ============== */

extern void kernelvec(void);

#endif /* TRAP_H */





