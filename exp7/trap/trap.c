/*
 * trap.c - 中断与异常处理实现
 */

#include "../include/trap.h"
#include "../include/printf.h"
#include "../include/syscall.h"
#include "../include/proc.h"

/* ============== 常量 ============== */

#define MAX_IRQ             16
#define MAX_EXC             16
#define MAX_SHARED_HANDLERS 4

/* ============== 数据结构 ============== */

struct shared_irq_handler {
    shared_handler_t handler;
    void *dev_id;
    const char *name;
};

struct irq_desc {
    interrupt_handler_t handler;
    int priority;
    struct shared_irq_handler shared[MAX_SHARED_HANDLERS];
    int shared_count;
    uint64 count, total_cycles, max_cycles;
    int nested_count;
};

/* ============== 全局变量 ============== */

static volatile uint64 ticks;
static struct irq_desc irq_descs[MAX_IRQ];
static exception_handler_t exc_handlers[MAX_EXC];
static volatile int nesting_level = 0;
static volatile int current_priority = IRQ_PRIO_LEVELS;
static interrupt_handler_t irq_handlers[MAX_IRQ];  /* 兼容旧接口 */

/* ============== 时钟 ============== */

uint64 get_time(void)  { return r_time(); }
uint64 get_ticks(void) { return ticks; }
void set_next_timer(void) { /* M模式处理 */ }

void timer_interrupt(void) {
    ticks++;
    w_sip(r_sip() & ~SIP_SSIP);  /* 清除软件中断挂起位 */
}

/* ============== 中断注册 ============== */

void register_interrupt_handler(int irq, interrupt_handler_t handler) {
    if (irq < 0 || irq >= MAX_IRQ) return;
    irq_handlers[irq] = handler;
    irq_descs[irq].handler = handler;
    irq_descs[irq].priority = IRQ_PRIO_NORMAL;
}

void register_interrupt_handler_prio(int irq, interrupt_handler_t handler, int prio) {
    if (irq < 0 || irq >= MAX_IRQ) return;
    irq_handlers[irq] = handler;
    irq_descs[irq].handler = handler;
    irq_descs[irq].priority = (prio >= 0 && prio < IRQ_PRIO_LEVELS) ? prio : IRQ_PRIO_NORMAL;
}

void register_exception_handler(int exc, exception_handler_t handler) {
    if (exc >= 0 && exc < MAX_EXC) exc_handlers[exc] = handler;
}

void set_irq_priority(int irq, int prio) {
    if (irq >= 0 && irq < MAX_IRQ && prio >= 0 && prio < IRQ_PRIO_LEVELS)
        irq_descs[irq].priority = prio;
}

int get_irq_priority(int irq) {
    return (irq >= 0 && irq < MAX_IRQ) ? irq_descs[irq].priority : IRQ_PRIO_NORMAL;
}

void enable_interrupt(int irq) {
    if (irq == IRQ_S_SOFT)  w_sie(r_sie() | SIE_SSIE);
    if (irq == IRQ_S_TIMER) w_sie(r_sie() | SIE_STIE);
    if (irq == IRQ_S_EXT)   w_sie(r_sie() | SIE_SEIE);
}

void disable_interrupt(int irq) {
    if (irq == IRQ_S_SOFT)  w_sie(r_sie() & ~SIE_SSIE);
    if (irq == IRQ_S_TIMER) w_sie(r_sie() & ~SIE_STIE);
    if (irq == IRQ_S_EXT)   w_sie(r_sie() & ~SIE_SEIE);
}

/* ============== 共享中断 ============== */

int request_shared_irq(int irq, shared_handler_t handler, void *dev_id, const char *name) {
    if (irq < 0 || irq >= MAX_IRQ || !handler) return -1;
    struct irq_desc *desc = &irq_descs[irq];
    if (desc->shared_count >= MAX_SHARED_HANDLERS) return -1;
    for (int i = 0; i < desc->shared_count; i++)
        if (desc->shared[i].dev_id == dev_id) return -1;
    int idx = desc->shared_count++;
    desc->shared[idx].handler = handler;
    desc->shared[idx].dev_id = dev_id;
    desc->shared[idx].name = name;
    return 0;
}

void free_shared_irq(int irq, void *dev_id) {
    if (irq < 0 || irq >= MAX_IRQ) return;
    struct irq_desc *desc = &irq_descs[irq];
    for (int i = 0; i < desc->shared_count; i++) {
        if (desc->shared[i].dev_id == dev_id) {
            for (int j = i; j < desc->shared_count - 1; j++)
                desc->shared[j] = desc->shared[j + 1];
            desc->shared_count--;
            return;
        }
    }
}

/* ============== 嵌套管理 ============== */

int get_irq_nesting_level(void)   { return nesting_level; }
int get_current_irq_priority(void){ return current_priority; }
static int can_preempt(int irq)   { return irq_descs[irq].priority < current_priority; }

/* ============== 中断处理 ============== */

static void call_shared_handlers(int irq) {
    struct irq_desc *desc = &irq_descs[irq];
    for (int i = 0; i < desc->shared_count; i++)
        if (desc->shared[i].handler && desc->shared[i].handler(desc->shared[i].dev_id))
            break;
}

static void handle_irq(int irq) {
    if (irq < 0 || irq >= MAX_IRQ) return;
    struct irq_desc *desc = &irq_descs[irq];
    uint64 start = r_time();
    
    int saved_prio = current_priority, saved_nest = nesting_level;
    if (nesting_level > 0) desc->nested_count++;
    current_priority = desc->priority;
    nesting_level++;
    
    /* 非最高优先级中断允许被更高优先级抢占 */
    if (desc->priority > IRQ_PRIO_HIGHEST) {
        intr_on();
    }
    
    if (desc->handler) desc->handler();
    call_shared_handlers(irq);
    
    intr_off();
    
    nesting_level = saved_nest;
    current_priority = saved_prio;
    
    uint64 elapsed = r_time() - start;
    desc->count++;
    desc->total_cycles += elapsed;
    if (elapsed > desc->max_cycles) desc->max_cycles = elapsed;
}

int devintr(void) {
    uint64 scause = r_scause();
    if (!(scause & SCAUSE_INTERRUPT)) return 0;
    
    int irq = scause & ~SCAUSE_INTERRUPT;
    
    /* 检查是否可以抢占当前中断 */
    if (nesting_level > 0 && !can_preempt(irq)) {
        return 0;  /* 优先级不够，不抢占 */
    }
    
    switch (irq) {
    case IRQ_S_SOFT:
        /* 软件中断：清除挂起位 */
        w_sip(r_sip() & ~SIP_SSIP);
        ticks++;
        
        /* 如果在嵌套中，只处理高优先级的 SOFT handler */
        if (nesting_level > 0) {
            if (irq_descs[IRQ_S_SOFT].handler) {
                handle_irq(IRQ_S_SOFT);
            }
        } else {
            /* 顶层：先处理 TIMER（低优先级），它会开中断允许抢占 */
            handle_irq(IRQ_S_TIMER);
            /* TIMER 返回后，处理 SOFT handler */
            if (irq_descs[IRQ_S_SOFT].handler) {
                handle_irq(IRQ_S_SOFT);
            }
        }
        return IRQ_S_SOFT;
    case IRQ_S_TIMER:
        timer_interrupt();
        handle_irq(IRQ_S_TIMER);
        return IRQ_S_TIMER;
    case IRQ_S_EXT:
        handle_irq(IRQ_S_EXT);
        return IRQ_S_EXT;
    default:
        return 0;
    }
}

/* ============== 异常处理 ============== */

static const char *exc_names[MAX_EXC] = {
    [0]="InstMisalign", [1]="InstFault", [2]="IllegalInst", [3]="Breakpoint",
    [4]="LoadMisalign", [5]="LoadFault", [6]="StoreMisalign", [7]="StoreFault",
    [8]="EcallU", [9]="EcallS", [12]="InstPageFault", [13]="LoadPageFault",
    [15]="StorePageFault",
};

static void default_exception_handler(struct trapframe *tf) {
    uint64 c = tf->scause;
    printf("\n=== EXCEPTION: %s (cause=%lu) ===\n", 
           (c < MAX_EXC && exc_names[c]) ? exc_names[c] : "Unknown", c);
    printf("sepc=0x%lx stval=0x%lx\n", tf->sepc, tf->stval);
    printf("System halted.\n");
    while (1) __asm__ volatile("wfi");
}

static void handle_exception(struct trapframe *tf) {
    uint64 cause = tf->scause;
    
    /* 系统调用处理 (ecall from U-mode or S-mode) */
    if (cause == EXC_ECALL_U || cause == EXC_ECALL_S) {
        /* sepc 指向 ecall 指令，需要 +4 跳过 */
        tf->sepc += 4;
        
        /* 调用系统调用分发器 */
        struct proc *p = myproc();
        if (p && p->trapframe) {
            /* 同步 trapframe */
            p->trapframe->sepc = tf->sepc;
            p->trapframe->a7 = tf->a7;
            p->trapframe->a0 = tf->a0;
            p->trapframe->a1 = tf->a1;
            p->trapframe->a2 = tf->a2;
            p->trapframe->a3 = tf->a3;
            p->trapframe->a4 = tf->a4;
            p->trapframe->a5 = tf->a5;
            
            syscall();
            
            /* 将返回值复制回 tf */
            tf->a0 = p->trapframe->a0;
        }
        return;
    }
    
    if (cause < MAX_EXC && exc_handlers[cause])
        exc_handlers[cause](tf);
    else
        default_exception_handler(tf);
}

/* ============== 内核陷阱处理 ============== */

void kerneltrap(void) {
    uint64 sepc = r_sepc(), sstatus = r_sstatus(), scause = r_scause();
    
    if ((sstatus & SSTATUS_SPP) == 0) {
        printf("kerneltrap: not from S-mode\n");
        while (1);
    }
    
    if (scause & SCAUSE_INTERRUPT) {
        int irq = devintr();
        if (irq == 0 && nesting_level == 0) {
            printf("kerneltrap: unknown irq, scause=0x%lx\n", scause);
            while (1);
        }
        w_sepc(sepc);
        w_sstatus(sstatus);
    } else {
        struct trapframe tf;
        tf.sepc = sepc; tf.sstatus = sstatus; tf.scause = scause; tf.stval = r_stval();
        handle_exception(&tf);
        w_sepc(tf.sepc);
        w_sstatus(sstatus);
    }
}

/* ============== 初始化 ============== */

void trap_init(void) {
    ticks = nesting_level = 0;
    current_priority = IRQ_PRIO_LEVELS;
    for (int i = 0; i < MAX_IRQ; i++) {
        irq_handlers[i] = 0;
        irq_descs[i].handler = 0;
        irq_descs[i].priority = IRQ_PRIO_NORMAL;
        irq_descs[i].shared_count = 0;
        irq_descs[i].count = irq_descs[i].total_cycles = irq_descs[i].max_cycles = 0;
        irq_descs[i].nested_count = 0;
    }
    for (int i = 0; i < MAX_EXC; i++) exc_handlers[i] = 0;
}

void trap_inithart(void) {
    w_stvec((uint64)kernelvec);
    w_sie(r_sie() | SIE_SSIE);
    intr_on();
}

/* ============== 统计 ============== */

void get_irq_stats(int irq, struct irq_stats *stats) {
    if (irq < 0 || irq >= MAX_IRQ || !stats) return;
    struct irq_desc *d = &irq_descs[irq];
    stats->count = d->count;
    stats->total_cycles = d->total_cycles;
    stats->max_cycles = d->max_cycles;
    stats->priority = d->priority;
    stats->nested_count = d->nested_count;
}

void print_irq_stats(void) {
    printf("\n=== IRQ Stats ===\n");
    for (int i = 0; i < MAX_IRQ; i++) {
        struct irq_desc *d = &irq_descs[i];
        if (d->count > 0)
            printf("IRQ%d: cnt=%lu avg=%lu max=%lu prio=%d\n",
                   i, d->count, d->total_cycles/d->count, d->max_cycles, d->priority);
    }
}
