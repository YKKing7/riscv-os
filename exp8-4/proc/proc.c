/*
 * proc.c - 进程管理实现
 */

#include "../include/proc.h"
#include "../include/pmm.h"
#include "../include/riscv.h"
#include "../include/printf.h"
#include "../include/trap.h"

/* ============== 全局变量 ============== */

struct proc proc[NPROC];
struct cpu cpus[1];
struct proc *initproc = 0;

static int nextpid = 1;
static int intr_nesting = 0;
static int intr_was_enabled = 0;

/* 调度器状态 */
static int current_scheduler = SCHED_RR;
static uint64 last_boost_tick = 0;
static struct sched_stats stats = {0};

/* ============== 辅助函数 ============== */

static void strncpy_safe(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void memset_zero(void *dst, uint64 n) {
    char *p = (char*)dst;
    while (n-- > 0)
        *p++ = 0;
}

static const char* state_name(enum procstate s) {
    static const char *names[] = {
        "UNUSED", "USED", "RUNNABLE", "RUNNING", "SLEEPING", "ZOMBIE"
    };
    return (s <= ZOMBIE) ? names[s] : "?";
}

/* ============== 中断管理 ============== */

void push_off(void) {
    int old = intr_get();
    intr_off();
    if (intr_nesting == 0)
        intr_was_enabled = old;
    intr_nesting++;
}

void pop_off(void) {
    if (intr_get())
        return;
    intr_nesting--;
    if (intr_nesting == 0 && intr_was_enabled)
        intr_on();
}

struct cpu*  mycpu(void)  { return &cpus[0]; }
struct proc* myproc(void) { return mycpu()->proc; }

static int alloc_pid(void) { return nextpid++; }

/* ============== 进程管理 ============== */

void proc_init(void) {
    for (int i = 0; i < NPROC; i++) {
        proc[i].state = UNUSED;
        proc[i].pid = 0;
    }
    cpus[0].proc = 0;
}

static void forkret(void) {
    struct proc *p = myproc();
    intr_on();
    if (p->entry)
        p->entry();
    exit_process(0);
}

static struct proc* alloc_process(void) {
    push_off();
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p->state == UNUSED) {
            p->pid = alloc_pid();
            p->state = USED;
            pop_off();
            
            p->kstack = (uint64)alloc_page();
            if (p->kstack == 0) {
                push_off();
                p->state = UNUSED;
                p->pid = 0;
                pop_off();
                return 0;
            }
            
            memset_zero(&p->context, sizeof(p->context));
            p->context.sp = p->kstack + KSTACK_SIZE;
            p->context.ra = (uint64)forkret;
            p->chan = 0;
            p->killed = 0;
            p->xstate = 0;
            p->parent = 0;
            p->entry = 0;
            p->sched_count = 0;
            p->total_ticks = 0;
            p->priority = PRIO_DEFAULT;
            p->queue_level = 0;
            p->time_slice = 1;
            p->wait_start = 0;
            p->trapframe = 0;
            p->sz = 0;
            for (int i = 0; i < NVMA; i++)
                p->vmas[i].used = 0;
            return p;
        }
    }
    pop_off();
    return 0;
}

static void free_process(struct proc *p) {
    if (p == 0) return;
    if (p->kstack) {
        free_page((void*)p->kstack);
        p->kstack = 0;
    }
    push_off();
    p->state = UNUSED;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = '\0';
    pop_off();
}

int create_process(void (*entry)(void), const char *name) {
    struct proc *p = alloc_process();
    if (p == 0)
        return -1;
    
    push_off();
    p->entry = entry;
    p->parent = myproc();
    strncpy_safe(p->name, name ? name : "?", sizeof(p->name));
    p->state = RUNNABLE;
    int pid = p->pid;
    pop_off();
    return pid;
}

void exit_process(int status) {
    struct proc *p = myproc();
    if (p == 0 || p == initproc)
        while (1);
    
    push_off();
    /* 将子进程托管给 init */
    for (struct proc *child = proc; child < &proc[NPROC]; child++) {
        if (child->parent == p) {
            child->parent = initproc;
            if (child->state == ZOMBIE)
                wakeup(initproc);
        }
    }
    wakeup(p->parent);
    p->xstate = status;
    p->state = ZOMBIE;
    sched();
    pop_off();
    while (1);
}

int wait_process(int *status) {
    struct proc *p = myproc();
    if (p == 0)
        return -1;
    
    for (;;) {
        int havekids = 0;
        push_off();
        
        for (struct proc *child = proc; child < &proc[NPROC]; child++) {
            if (child->parent != p)
                continue;
            havekids = 1;
            if (child->state == ZOMBIE) {
                int pid = child->pid;
                if (status)
                    *status = child->xstate;
                child->state = UNUSED;
                pop_off();
                free_process(child);
                return pid;
            }
        }
        pop_off();
        
        if (!havekids)
            return -1;
        sleep(p);
    }
}

/* ============== 系统调用支持 ============== */

/* fork - 简化实现 */
int fork(void) {
    return -1;  /* 简化实现：不支持真正的 fork */
}

/* kill - 终止指定进程 */
int kill(int pid) {
    push_off();
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p->pid == pid) {
            p->killed = 1;
            if (p->state == SLEEPING)
                p->state = RUNNABLE;
            pop_off();
            return 0;
        }
    }
    pop_off();
    return -1;
}

/* ============== 调度器 ============== */

void sched(void) {
    struct proc *p = myproc();
    struct cpu *c = mycpu();
    if (p == 0)
        while (1);
    if (p->start_tick > 0) {
        p->total_ticks += (get_time() - p->start_tick) / 100000;  /* 转换为较小单位 */
        p->start_tick = 0;
    }
    swtch(&p->context, &c->context);
}

void yield(void) {
    struct proc *p = myproc();
    if (p == 0)
        return;
    push_off();
    p->state = RUNNABLE;
    p->wait_start = get_ticks();
    /* MLFQ: 用完时间片降级 */
    if (current_scheduler == SCHED_MLFQ && p->time_slice <= 0) {
        if (p->queue_level < MLFQ_LEVELS - 1)
            p->queue_level++;
    }
    sched();
    pop_off();
}

/* 调度器配置 */
void set_scheduler(int type) {
    if (type >= SCHED_RR && type <= SCHED_MLFQ)
        current_scheduler = type;
}

int get_scheduler(void) { return current_scheduler; }

const char* scheduler_name(int type) {
    static const char *names[] = {"RR", "Priority", "MLFQ"};
    return (type >= 0 && type <= 2) ? names[type] : "?";
}

/* 优先级管理 */
void set_priority(struct proc *p, int prio) {
    if (p && prio >= 0 && prio < PRIO_LEVELS)
        p->priority = prio;
}

int get_priority(struct proc *p) {
    return p ? p->priority : PRIO_DEFAULT;
}

int create_process_prio(void (*entry)(void), const char *name, int prio) {
    int pid = create_process(entry, name);
    if (pid > 0) {
        push_off();
        for (struct proc *p = proc; p < &proc[NPROC]; p++) {
            if (p->pid == pid) {
                p->priority = (prio >= 0 && prio < PRIO_LEVELS) ? prio : PRIO_DEFAULT;
                break;
            }
        }
        pop_off();
    }
    return pid;
}

/* 调度统计 */
void reset_sched_stats(void) {
    stats.total_switches = 0;
    stats.total_wait_time = 0;
    stats.total_turnaround = 0;
    stats.completed_procs = 0;
}

struct sched_stats get_sched_stats(void) { return stats; }

/* 轮转调度 - 从上次位置继续扫描 */
static int rr_last = 0;

static struct proc* pick_rr(void) {
    /* 从上次位置开始扫描一圈 */
    for (int i = 0; i < NPROC; i++) {
        int idx = (rr_last + i) % NPROC;
        if (proc[idx].state == RUNNABLE) {
            rr_last = (idx + 1) % NPROC;
            return &proc[idx];
        }
    }
    return 0;
}

/* 优先级调度 - 选择最高优先级进程 */
static struct proc* pick_priority(void) {
    struct proc *best = 0;
    int best_prio = PRIO_LEVELS;
    
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p->state == RUNNABLE && p->priority < best_prio) {
            best = p;
            best_prio = p->priority;
        }
    }
    return best;
}

/* MLFQ: 优先级提升 */
static void mlfq_boost(void) {
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p->state != UNUSED) {
            p->queue_level = 0;
            p->time_slice = 1;
        }
    }
}

/* MLFQ 调度 - 多级反馈队列 */
static struct proc* pick_mlfq(void) {
    uint64 now = get_ticks();
    
    /* 周期性提升所有进程优先级 */
    if (now - last_boost_tick >= MLFQ_BOOST_INTERVAL) {
        mlfq_boost();
        last_boost_tick = now;
    }
    
    /* 从最高优先级队列开始查找 */
    for (int level = 0; level < MLFQ_LEVELS; level++) {
        for (struct proc *p = proc; p < &proc[NPROC]; p++) {
            if (p->state == RUNNABLE && p->queue_level == level)
                return p;
        }
    }
    return 0;
}

/* 选择下一个进程 */
static struct proc* pick_next(void) {
    switch (current_scheduler) {
        case SCHED_PRIO: return pick_priority();
        case SCHED_MLFQ: return pick_mlfq();
        default:         return pick_rr();
    }
}

/* 运行选中的进程 */
static void run_proc(struct cpu *c, struct proc *p) {
    uint64 now = get_time();
    
    /* 统计等待时间 */
    if (p->wait_start > 0)
        stats.total_wait_time += now - p->wait_start;
    
    p->state = RUNNING;
    p->sched_count++;
    p->start_tick = now;
    p->time_slice = (current_scheduler == SCHED_MLFQ) ? (1 << p->queue_level) : 1;
    c->proc = p;
    stats.total_switches++;
    
    swtch(&c->context, &p->context);
    c->proc = 0;
}

void scheduler(void) {
    struct cpu *c = mycpu();
    c->proc = 0;
    
    for (;;) {
        intr_on();
        
        push_off();
        struct proc *p = pick_next();
        if (p)
            run_proc(c, p);
        pop_off();
        
        if (!p)
            __asm__ volatile("wfi");
    }
}

/* ============== 同步原语 ============== */

void sleep(void *chan) {
    struct proc *p = myproc();
    if (p == 0)
        while (1);
    push_off();
    p->chan = chan;
    p->state = SLEEPING;
    sched();
    p->chan = 0;
    pop_off();
}

void wakeup(void *chan) {
    push_off();
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p != myproc() && p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
    }
    pop_off();
}

/* ============== 调试 ============== */

/* 打印固定宽度字符串 */
static void print_str(const char *s, int width) {
    int len = 0;
    while (s[len]) { printf("%c", s[len]); len++; }
    while (len++ < width) printf(" ");
}

/* 打印固定宽度数字 */
static void print_int(uint64 n, int width) {
    char buf[16];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    else while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i < width) { printf(" "); width--; }
    while (i > 0) printf("%c", buf[--i]);
}

void debug_proc_table(void) {
    printf("\n  +-----+------------+----------+-------+--------+\n");
    printf("  | PID | Name       | State    | Sched | Parent |\n");
    printf("  +-----+------------+----------+-------+--------+\n");
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        if (p->state != UNUSED) {
            printf("  | ");
            print_int(p->pid, 3);
            printf(" | ");
            print_str(p->name[0] ? p->name : "-", 10);
            printf(" | ");
            print_str(state_name(p->state), 8);
            printf(" | ");
            print_int(p->sched_count, 5);
            printf(" | ");
            print_int(p->parent ? p->parent->pid : 0, 6);
            printf(" |\n");
        }
    }
    printf("  +-----+------------+----------+-------+--------+\n");
}
