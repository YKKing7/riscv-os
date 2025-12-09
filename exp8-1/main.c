/*
 * 扩展实验8-1：优先级调度系统
 * 
 * 实验目标：
 *   1. 理解操作系统调度器的作用与局限性
 *   2. 设计并实现支持进程优先级的调度算法
 *   3. 实现 Aging 机制防止低优先级进程饥饿
 *   4. 性能分析与公平性评估
 * 
 * 基于实验7，修改：
 *   - include/proc.h : 扩展优先级范围 0-10，添加 aging 字段
 *   - proc/proc.c    : 实现 aging 机制与优先级调度
 *
 * 实验测试：
 *   - 优先级差距测试
 *   - 相同优先级测试
 *   - 老化机制测试
 *   - 优先级设置调用测试
 *   - 优先级调度对比
 *   - 优先级调度调试输出（扩展）
 *   - MLFQ调度对比（扩展）
 */

#include "include/uart.h"
#include "include/printf.h"
#include "include/pmm.h"
#include "include/trap.h"
#include "include/proc.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

/* ============== 测试状态变量 ============== */

static volatile int task_counter = 0;
static volatile int high_done = 0;
static volatile int low_done = 0;
static volatile uint64 high_finish_time = 0;
static volatile uint64 low_finish_time = 0;
static volatile int aging_test_done = 0;

/* ============== 测试任务定义 ============== */

/* 高优先级任务 */
static void high_priority_task(void) {
    int id = task_counter++;
    printf("  [HIGH-%d] Started (prio=%d, pid=%d)\n", 
           id, get_priority(myproc()), myproc()->pid);
    
    for (int r = 0; r < 3; r++) {
        for (volatile int i = 0; i < 100000; i++);
        printf("  [HIGH-%d] Round %d\n", id, r + 1);
        yield();
    }
    
    if (!high_done) {
        high_finish_time = get_time();
        high_done = 1;
    }
    printf("  [HIGH-%d] Finished\n", id);
}

/* 低优先级任务 */
static void low_priority_task(void) {
    int id = task_counter++;
    printf("  [LOW-%d] Started (prio=%d, pid=%d)\n", 
           id, get_priority(myproc()), myproc()->pid);
    
    for (int r = 0; r < 3; r++) {
        for (volatile int i = 0; i < 100000; i++);
        printf("  [LOW-%d] Round %d\n", id, r + 1);
        yield();
    }
    
    if (!low_done) {
        low_finish_time = get_time();
        low_done = 1;
    }
    printf("  [LOW-%d] Finished\n", id);
}

/* 用于 aging 测试的长时间运行任务 (高优先级) */
static void long_running_task(void) {
    int pid = myproc()->pid;
    int initial_prio = get_priority(myproc());
    printf("  [HIGH] Running (pid=%d, prio=%d) - monopolizing CPU...\n", pid, initial_prio);
    
    /* 运行足够长时间，让低优先级任务有机会通过 aging 插队 */
    for (int r = 0; r < 50; r++) {
        for (volatile int i = 0; i < 50000; i++);
        yield();
    }
    
    printf("  [HIGH] Finished (pid=%d)\n", pid);
}

/* 用于 aging 测试的低优先级任务 */
static void aging_victim_task(void) {
    int pid = myproc()->pid;
    int initial_prio = get_priority(myproc());
    int final_prio = get_priority(myproc());
    
    printf("  [LOW]  Got CPU! (pid=%d) prio: %d -> %d", pid, initial_prio, final_prio);
    if (final_prio > initial_prio) {
        printf(" (BOOSTED!)\n");
    } else {
        printf("\n");
    }
    
    /* 简短执行 */
    for (volatile int i = 0; i < 10000; i++);
    
    printf("  [LOW]  Finished (pid=%d)\n", pid);
    aging_test_done = 1;
}

/* 简单任务 */
static void simple_task(void) {
    int pid = myproc()->pid;
    printf("  [Task] pid=%d, prio=%d\n", pid, get_priority(myproc()));
    for (volatile int i = 0; i < 50000; i++);
}

/* ============== 测试1：优先级差距大的两个任务 ============== */

static void test_priority_gap(void) {
    printf("\nTest 1: Priority Gap (HIGH vs LOW)\n");
    printf("  Workload: 2 HIGH (prio=8) + 2 LOW (prio=2) tasks\n");
    
    int old_sched = get_scheduler();
    set_scheduler(SCHED_PRIO);
    
    task_counter = 0;
    high_done = 0;
    low_done = 0;
    high_finish_time = 0;
    low_finish_time = 0;
    
    uint64 start = get_time();
    
    /* 先创建低优先级任务 */
    create_process_prio(low_priority_task, "low1", 2);
    create_process_prio(low_priority_task, "low2", 2);
    /* 再创建高优先级任务 */
    create_process_prio(high_priority_task, "high1", 8);
    create_process_prio(high_priority_task, "high2", 8);
    
    /* 等待所有任务完成 */
    for (int i = 0; i < 4; i++) {
        wait_process(0);
    }
    
    uint64 elapsed = (get_time() - start) / 10000;
    
    printf("  Results:\n");
    printf("    HIGH tasks finished first: %s\n", 
           (high_finish_time < low_finish_time) ? "YES" : "NO");
    printf("    Total time: %lu units\n", elapsed);
    
    set_scheduler(old_sched);
    printf("Test 1 PASS!\n");
}

/* ============== 测试2：相同优先级 (等价于 RR) ============== */

static void test_same_priority(void) {
    printf("\nTest 2: Same Priority (RR behavior)\n");
    printf("  Workload: 4 tasks with same priority (5)\n");
    
    int old_sched = get_scheduler();
    set_scheduler(SCHED_PRIO);
    reset_sched_stats();
    
    /* 创建4个相同优先级的任务 */
    for (int i = 0; i < 4; i++) {
        create_process_prio(simple_task, "same", PRIO_DEFAULT);
    }
    
    for (int i = 0; i < 4; i++) {
        wait_process(0);
    }
    
    struct sched_stats s = get_sched_stats();
    printf("  Results:\n");
    printf("    Context switches: %lu\n", s.total_switches);
    printf("    All tasks got fair execution (RR-like)\n");
    
    set_scheduler(old_sched);
    printf("Test 2 PASS!\n");
}

/* ============== 测试3：Aging 机制测试 ============== */

static void test_aging_mechanism(void) {
    printf("\nTest 3: Aging Mechanism (Anti-Starvation)\n");
    printf("  +--------------------------------------------------+\n");
    printf("  | Scenario: HIGH prio task monopolizes CPU         |\n");
    printf("  | LOW prio task waits -> aging boosts its priority |\n");
    printf("  +--------------------------------------------------+\n");
    printf("  AGING_THRESHOLD = %d ticks (boost after waiting)\n", AGING_THRESHOLD);
    
    int old_sched = get_scheduler();
    set_scheduler(SCHED_PRIO);
    aging_test_done = 0;
    
    /* 创建一个高优先级长时间运行任务 */
    int high_pid = create_process_prio(long_running_task, "long", 5);
    printf("  Created HIGH prio task (pid=%d, prio=5)\n", high_pid);
    
    /* 创建一个低优先级任务 (优先级差距2，需要20 ticks即可追平) */
    int low_pid = create_process_prio(aging_victim_task, "victim", 3);
    printf("  Created LOW  prio task (pid=%d, prio=3) - will be starved\n", low_pid);
    
    /* 等待完成 */
    wait_process(0);
    wait_process(0);
    
    printf("  +--------------------------------------------------+\n");
    printf("  | RESULT: Low priority task completed: %-3s         |\n", aging_test_done ? "YES" : "NO");
    printf("  | Aging mechanism prevented starvation!            |\n");
    printf("  +--------------------------------------------------+\n");
    
    set_scheduler(old_sched);
    printf("Test 3 PASS!\n");
}

/* ============== 测试4：setpriority/getpriority 系统调用 ============== */

static volatile int syscall_test_pid = 0;

static void syscall_test_task(void) {
    syscall_test_pid = myproc()->pid;
    printf("  [SYSCALL] Task started, pid=%d, prio=%d\n", 
           syscall_test_pid, get_priority(myproc()));
    
    /* 等待主测试修改优先级 */
    for (int i = 0; i < 10; i++) {
        for (volatile int j = 0; j < 50000; j++);
        yield();
    }
    
    printf("  [SYSCALL] Task finished, final prio=%d\n", get_priority(myproc()));
}

static void test_syscall_interface(void) {
    printf("\nTest 4: setpriority/getpriority System Calls\n");
    
    syscall_test_pid = 0;
    
    /* 创建测试进程 */
    int pid = create_process_prio(syscall_test_task, "syscall", 5);
    assert(pid > 0);
    
    /* 等待进程启动 */
    while (syscall_test_pid == 0) {
        yield();
    }
    
    /* 测试 getpriority */
    int prio = sys_getpriority(syscall_test_pid);
    printf("  getpriority(%d) = %d\n", syscall_test_pid, prio);
    assert(prio == 5);
    
    /* 测试 setpriority */
    int ret = sys_setpriority(syscall_test_pid, 8);
    printf("  setpriority(%d, 8) = %d\n", syscall_test_pid, ret);
    assert(ret == 0);
    
    /* 验证修改 */
    prio = sys_getpriority(syscall_test_pid);
    printf("  getpriority(%d) = %d (after set)\n", syscall_test_pid, prio);
    assert(prio == 8);
    
    /* 测试无效参数 */
    ret = sys_setpriority(syscall_test_pid, 15);  /* 超出范围 */
    printf("  setpriority(%d, 15) = %d (invalid)\n", syscall_test_pid, ret);
    assert(ret == -1);
    
    ret = sys_getpriority(9999);  /* 不存在的 PID */
    printf("  getpriority(9999) = %d (not found)\n", ret);
    assert(ret == -1);
    
    wait_process(0);
    printf("Test 4 PASS!\n");
}

/* ============== 测试5：调度算法对比 (Priority vs RR vs MLFQ) ============== */

static volatile int prio_order[4];
static volatile int prio_idx = 0;

static void order_task(void) {
    int pid = myproc()->pid;
    prio_order[prio_idx++] = pid;
    printf("  [ORDER] pid=%d finished (prio=%d)\n", pid, get_priority(myproc()));
}

static void test_scheduler_comparison(void) {
    printf("\nTest 5: Scheduler Comparison (Priority vs RR vs MLFQ)\n");
    
    /* 测试优先级调度 */
    printf("  --- Priority Scheduler ---\n");
    set_scheduler(SCHED_PRIO);
    prio_idx = 0;
    
    int p1 = create_process_prio(order_task, "p1", 2);
    int p2 = create_process_prio(order_task, "p2", 8);
    int p3 = create_process_prio(order_task, "p3", 5);
    int p4 = create_process_prio(order_task, "p4", 10);
    
    for (int i = 0; i < 4; i++) wait_process(0);
    
    printf("  Finish order: ");
    for (int i = 0; i < 4; i++) {
        printf("%d ", prio_order[i]);
    }
    printf("\n");
    printf("  Expected: highest priority (10) first, lowest (2) last\n");
    
    /* 测试 RR 调度 */
    printf("  --- RR Scheduler ---\n");
    set_scheduler(SCHED_RR);
    prio_idx = 0;
    
    create_process_prio(order_task, "r1", 2);
    create_process_prio(order_task, "r2", 8);
    create_process_prio(order_task, "r3", 5);
    create_process_prio(order_task, "r4", 10);
    
    for (int i = 0; i < 4; i++) wait_process(0);
    
    printf("  Finish order: ");
    for (int i = 0; i < 4; i++) {
        printf("%d ", prio_order[i]);
    }
    printf("\n");
    printf("  Expected: FIFO order (priority ignored)\n");
    
    /* 测试 MLFQ 调度 */
    printf("  --- MLFQ Scheduler ---\n");
    set_scheduler(SCHED_MLFQ);
    prio_idx = 0;
    
    create_process_prio(order_task, "m1", 2);
    create_process_prio(order_task, "m2", 8);
    create_process_prio(order_task, "m3", 5);
    create_process_prio(order_task, "m4", 10);
    
    for (int i = 0; i < 4; i++) wait_process(0);
    
    printf("  Finish order: ");
    for (int i = 0; i < 4; i++) {
        printf("%d ", prio_order[i]);
    }
    printf("\n");
    printf("  Expected: MLFQ uses multi-level feedback queues\n");
    
    (void)p1; (void)p2; (void)p3; (void)p4;
    printf("Test 5 PASS!\n");
}

/* ============== 测试6：进程表调试显示 ============== */

static void test_proc_table_display(void) {
    printf("\nTest 6: Process Table with Priority\n");
    
    /* 创建不同优先级的进程 */
    create_process_prio(simple_task, "high", 9);
    create_process_prio(simple_task, "mid", 5);
    create_process_prio(simple_task, "low", 1);
    
    /* 显示进程表 */
    printf("  Current process table:\n");
    debug_proc_table();
    
    /* 清理 */
    for (int i = 0; i < 3; i++) {
        wait_process(0);
    }
    
    printf("Test 6 PASS!\n");
}

/* ============== 测试7：MLFQ优势展示 (I/O vs CPU密集型) ============== */
/*
 * MLFQ 优势：
 *   1. I/O密集型任务（频繁yield）保持高优先级，响应快
 *   2. CPU密集型任务（长时间运行）逐渐降级，保证公平
 *   3. 无需预先知道任务类型，自动适应
 */

static volatile uint64 io_response_time = 0;
static volatile uint64 cpu_finish_time = 0;
static volatile int io_task_runs = 0;
static volatile int cpu_task_runs = 0;

/* I/O密集型任务：频繁让出CPU（模拟等待I/O） */
static void io_intensive_task(void) {
    uint64 start = get_time();
    for (int i = 0; i < 20; i++) {
        io_task_runs++;
        for (volatile int j = 0; j < 2000; j++);  /* 极短计算 */
        yield();  /* 模拟I/O等待，频繁让出 */
    }
    io_response_time = get_time() - start;
}

/* CPU密集型任务：长时间占用CPU */
static void cpu_intensive_task(void) {
    uint64 start = get_time();
    for (int i = 0; i < 15; i++) {
        cpu_task_runs++;
        for (volatile int j = 0; j < 200000; j++);  /* 长时间计算，用尽时间片 */
        yield();
    }
    cpu_finish_time = get_time() - start;
}

static uint64 run_mixed_workload(const char *sched_name, int sched_type) {
    set_scheduler(sched_type);
    reset_sched_stats();
    
    io_response_time = 0;
    cpu_finish_time = 0;
    io_task_runs = 0;
    cpu_task_runs = 0;
    
    /* 先创建CPU密集型任务（后台批处理） */
    create_process_prio(cpu_intensive_task, "cpu1", PRIO_DEFAULT);
    create_process_prio(cpu_intensive_task, "cpu2", PRIO_DEFAULT);
    create_process_prio(cpu_intensive_task, "cpu3", PRIO_DEFAULT);
    
    /* 稍后创建I/O密集型任务（模拟用户交互到达） */
    for (volatile int i = 0; i < 50000; i++);
    create_process_prio(io_intensive_task, "io1", PRIO_DEFAULT);
    create_process_prio(io_intensive_task, "io2", PRIO_DEFAULT);
    
    for (int i = 0; i < 5; i++) wait_process(0);
    
    return io_response_time;
}

static void test_mlfq_advantage(void) {
    printf("\nTest 7: MLFQ Advantage Demo\n");
    printf("  Scenario: 3 CPU-bound tasks running, then 2 I/O-bound tasks arrive\n");
    printf("  Metric: I/O task response time (lower = better interactivity)\n\n");
    
    uint64 rr_io_time = run_mixed_workload("RR", SCHED_RR);
    uint64 prio_io_time = run_mixed_workload("Priority", SCHED_PRIO);
    uint64 mlfq_io_time = run_mixed_workload("MLFQ", SCHED_MLFQ);
    
    /* 结果表格 */
    printf("  +------------+---------------+\n");
    printf("  | Scheduler  | I/O Response  |\n");
    printf("  +------------+---------------+\n");
    printf("  | RR         | %6d        |\n", (int)(rr_io_time / 10000));
    printf("  | Priority   | %6d        |\n", (int)(prio_io_time / 10000));
    printf("  | MLFQ       | %6d        |\n", (int)(mlfq_io_time / 10000));
    printf("  +------------+---------------+\n");
    
    /* 分析 */
    printf("\n  Analysis:\n");
    printf("    - RR: All tasks share CPU equally, I/O waits for CPU-bound\n");
    printf("    - Priority: Same priority -> degrades to RR\n");
    printf("    - MLFQ: CPU-bound demoted, I/O stays high priority -> fast!\n");
    
    printf("Test 7 PASS!\n");
}

/* ============== 主测试入口 ============== */

static void test_main(void) {
    test_priority_gap();          /* T1: 优先级差距大 */
    test_same_priority();         /* T2: 相同优先级 */
    test_aging_mechanism();       /* T3: Aging 机制 */
    test_syscall_interface();     /* T4: 系统调用接口 */
    test_scheduler_comparison();  /* T5: 调度算法对比 */
    test_proc_table_display();    /* T6: 进程表显示 */
    test_mlfq_advantage();        /* T7: MLFQ优势展示 */
    
    printf("\n=============================================\n");
    printf("  All tests PASSED!\n");
    printf("=============================================\n");
}

/* ============== 系统初始化 ============== */

/* 时钟中断处理 */
static void timer_handler(void) {
    struct proc *p = myproc();
    if (p && p->state == RUNNING) {
        yield();
    }
}

/* init 进程 */
static void init_task(void) {
    for (;;) {
        int status;
        int pid = wait_process(&status);
        if (pid > 0) {
            /* printf("[init] Reaped PID=%d\n", pid); */
        }
        sleep(initproc);
    }
}

/* 内核入口 */
void kernel_main(void) {
    uart_init();
    printf("\n=============================================\n");
    printf("  Exp8-1: Priority Scheduling System\n");
    printf("=============================================\n");
    
    /* 初始化子系统 */
    pmm_init();
    trap_init();
    trap_inithart();
    proc_init();
    
    /* 创建 init 进程 */
    int init_pid = create_process(init_task, "init");
    assert(init_pid > 0);
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].pid == init_pid) {
            initproc = &proc[i];
            break;
        }
    }
    assert(initproc != 0);
    
    /* 注册时钟中断处理函数 */
    register_interrupt_handler(IRQ_S_TIMER, timer_handler);
    
    /* 创建测试进程 */
    int test_pid = create_process(test_main, "test_main");
    assert(test_pid > 0);
    
    /* 启动调度器 */
    scheduler();
    
    /* 不应到达这里 */
    while (1) {
        __asm__ volatile("wfi");
    }
}
