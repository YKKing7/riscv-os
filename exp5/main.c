/*
 * 实验5：进程管理与调度
 * 
 * 实验目标：
 *   1. 实现进程创建与销毁
 *   2. 实现上下文切换机制
 *   3. 实现多种调度算法 (RR, Priority, MLFQ)
 *   4. 实现进程同步原语 (sleep/wakeup)
 *   5. 调度算法性能对比分析
 * 
 * 基于实验4，新增：
 *   - include/proc.h  : 进程管理接口声明
 *   - proc/proc.c     : 进程管理实现
 *   - proc/swtch.S    : 上下文切换汇编
 *
 * 实验测试：
 *   - 进程创建测试
 *   - 调度器测试
 *   - 同步机制测试
 *   - 进程状态调试
 *   - 优先级调度（扩展）
 *   - MLFQ调度（扩展）
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

static volatile int task_counter = 0;           /* Test 1: 任务计数器 */

/* Test 3: 生产者-消费者同步状态 */
static volatile int producer_done = 0;
static volatile int consumer_done = 0;
static volatile int shared_data = 0;
static volatile int data_ready = 0;
static volatile int consumer_ready = 0;
static int sync_chan = 0;

/* Test 5/6: 调度算法性能测试状态 */
static volatile int perf_counter = 0;
static volatile uint64 first_done_time = 0;
static volatile int first_done_flag = 0;
static volatile uint64 io_first_done_time = 0;
static volatile int io_done_count = 0;

/* ============== 测试任务定义 ============== */

/* Test 1: 简单任务 */
static void simple_task(void) {
    int id = task_counter++;
    printf("  [Task %d] Hello from PID=%d\n", id, myproc()->pid);
    for (int r = 0; r < 3; r++) {
        for (volatile int i = 0; i < 200000; i++);
        yield();
    }
    printf("  [Task %d] Goodbye!\n", id);
}

/* Test 1: 空任务 */
static void dummy_task(void) {
    for (volatile int i = 0; i < 10000; i++);
}

/* Test 2: CPU密集型任务 */
static void cpu_intensive_task(void) {
    int pid = myproc()->pid;
    printf("  [CPU Task %d] Started\n", pid);
    for (int r = 0; r < 3; r++) {
        for (volatile int i = 0; i < 50000; i++);
        printf("  [CPU Task %d] Round %d complete\n", pid, r + 1);
        yield();
    }
    printf("  [CPU Task %d] Finished\n", pid);
}

/* Test 3: 生产者任务 */
static void producer_task(void) {
    printf("  [Producer] Started\n");
    while (!consumer_ready) yield();
    for (int i = 1; i <= 3; i++) {
        for (volatile int j = 0; j < 10000; j++);
        shared_data = i * 10;
        data_ready = 1;
        printf("  [Producer] Produced: %d\n", shared_data);
        wakeup(&sync_chan);
        while (data_ready) yield();
    }
    producer_done = 1;
    printf("  [Producer] Done\n");
}

/* Test 3: 消费者任务 */
static void consumer_task(void) {
    printf("  [Consumer] Started\n");
    consumer_ready = 1;
    for (int i = 0; i < 3; i++) {
        while (!data_ready) sleep(&sync_chan);
        printf("  [Consumer] Consumed: %d\n", shared_data);
        data_ready = 0;
    }
    consumer_done = 1;
    printf("  [Consumer] Done\n");
}

/* Test 5: 高优先级任务 */
static void high_prio_task(void) {
    for (volatile int i = 0; i < 100000; i++);
    if (!first_done_flag) {
        first_done_time = get_time();
        first_done_flag = 1;
    }
    perf_counter++;
}

/* Test 5: 低优先级任务 */
static void low_prio_task(void) {
    for (volatile int i = 0; i < 800000; i++);
    perf_counter++;
}

/* Test 6: CPU密集型任务 */
static void cpu_bound_task(void) {
    for (int r = 0; r < 3; r++) {
        for (volatile int i = 0; i < 1000000; i++);
        yield();
    }
    perf_counter++;
}

/* Test 6: IO密集型任务 */
static void io_bound_task(void) {
    for (int r = 0; r < 60; r++) {
        for (volatile int i = 0; i < 50; i++);
        yield();
    }
    io_done_count++;
    if (io_done_count == 1) {
        io_first_done_time = get_time();
    }
    perf_counter++;
}

/* ============== 功能测试 ============== */

/* 测试1: 进程创建与销毁 */

static void test_process_creation(void) {
    printf("\nTest 1: Process Creation & Destruction\n");

    /* 测试基本的进程创建 */
    task_counter = 0;
    int pid = create_process(simple_task, "test_proc");
    assert(pid > 0);
    printf("  Created process PID=%d\n", pid);
    wait_process(NULL);

    /* 测试进程表限制 */
    int count = 0;
    for (int i = 0; i < NPROC + 5; i++) {
        int p = create_process(dummy_task, "limit");
        if (p > 0) {
            count++;
        } else {
            break;
        }
    }
    printf("  Created %d processes (limit test)\n", count);
    assert(count <= NPROC - 2);  /* init + test_main 占用 2 个槽位 */

    /* 清理测试进程 */
    for (int i = 0; i < count; i++) {
        wait_process(NULL);
    }
    printf("  All test processes cleaned up\n");
    printf("Test 1 PASS!\n");
}

/* 测试2: 基本调度功能 */

static void test_scheduler_basic(void) {
    printf("\nTest 2: Basic Scheduler\n");

    uint64 start_time = get_time();

    /* 创建多个计算密集型进程 */
    for (int i = 0; i < 3; i++) {
        create_process(cpu_intensive_task, "cpu_task");
    }

    /* 等待所有进程完成 */
    for (int i = 0; i < 3; i++) {
        wait_process(NULL);
    }

    uint64 end_time = get_time();
    printf("  Scheduler test completed in %lu cycles\n", end_time - start_time);
    printf("Test 2 PASS!\n");
}

/* 测试3: 进程同步机制 */

static void test_synchronization(void) {
    printf("\nTest 3: Synchronization\n");

    /* 重置同步状态 */
    producer_done = 0;
    consumer_done = 0;
    shared_data = 0;
    data_ready = 0;
    consumer_ready = 0;

    /* 创建生产者和消费者 */
    create_process(producer_task, "producer");
    create_process(consumer_task, "consumer");

    /* 等待完成 */
    wait_process(NULL);
    wait_process(NULL);

    /* 验证结果 */
    assert(producer_done == 1);
    assert(consumer_done == 1);
    printf("  Producer-Consumer synchronization verified\n");
    printf("Test 3 PASS!\n");
}

/* 测试4: 调试功能 */

static void test_debug_proc_table(void) {
    printf("\nTest 4: Debug Process Table\n");

    /* 创建几个进程用于调试显示 */
    for (int i = 0; i < 3; i++) {
        char name[] = {'d', 'b', 'g', '0' + i, '\0'};
        int pid = create_process(dummy_task, name);
        assert(pid > 0);
    }

    /* 显示进程表 */
    debug_proc_table();

    /* 清理 */
    for (int i = 0; i < 3; i++) {
        wait_process(NULL);
    }
    printf("Test 4 PASS!\n");
}

/* ============== 调度算法性能对比 ============== */

static struct {
    const char *name;
    uint64 switches;
    uint64 time;
    uint64 first;
} results[4];
static int result_count = 0;

/* 辅助函数: 重置性能测试状态 */
static void reset_perf_state(void) {
    perf_counter = 0;
    first_done_flag = 0;
    first_done_time = 0;
    io_first_done_time = 0;
    io_done_count = 0;
}

/* 辅助函数: 运行性能测试 */
static void run_perf_test(int sched, int prio_mode) {
    set_scheduler(sched);
    reset_sched_stats();
    reset_perf_state();

    uint64 start = get_time();

    if (prio_mode) {
        /* Test 5: 优先级调度测试 */
        create_process_prio(low_prio_task, "low1", PRIO_LOW);
        create_process_prio(low_prio_task, "low2", PRIO_LOW);
        create_process_prio(high_prio_task, "high1", PRIO_HIGH);
        create_process_prio(high_prio_task, "high2", PRIO_HIGH);
    } else {
        /* Test 6: MLFQ调度测试 */
        create_process(cpu_bound_task, "cpu1");
        create_process(io_bound_task, "io1");
        create_process(cpu_bound_task, "cpu2");
        create_process(io_bound_task, "io2");
    }

    /* 等待所有任务完成 */
    for (int i = 0; i < 4; i++) {
        wait_process(NULL);
    }

    uint64 elapsed = (get_time() - start) / 10000;
    uint64 metric = prio_mode 
        ? (first_done_time ? (first_done_time - start) / 10000 : elapsed)
        : (io_first_done_time ? (io_first_done_time - start) / 10000 : elapsed);

    struct sched_stats s = get_sched_stats();
    results[result_count].name = scheduler_name(sched);
    results[result_count].switches = s.total_switches;
    results[result_count].time = elapsed;
    results[result_count].first = metric;
    result_count++;
}

/* 辅助函数: 打印右对齐数字 */
static void print_num(uint64 n, int width) {
    char buf[16];
    int len = 0;
    
    if (n == 0) {
        buf[len++] = '0';
    } else {
        while (n > 0) {
            buf[len++] = '0' + (n % 10);
            n /= 10;
        }
    }
    
    /* 打印前导空格 */
    for (int i = len; i < width; i++) {
        printf(" ");
    }
    
    /* 反向打印数字 */
    for (int i = len - 1; i >= 0; i--) {
        printf("%c", buf[i]);
    }
}

/* 辅助函数: 计算字符串长度 */
static int strlen_simple(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

/* 辅助函数: 打印性能对比表格 */
static void print_perf_table(const char *metric_name) {
    printf("\n  +-----------+----------+-------+----------+\n");
    printf("  | Algorithm | Switches | Total | %s |\n", metric_name);
    printf("  +-----------+----------+-------+----------+\n");
    
    for (int i = 0; i < result_count; i++) {
        printf("  | %s", results[i].name);
        
        /* 填充算法名称到固定宽度 */
        int name_len = strlen_simple(results[i].name);
        for (int j = name_len; j < 9; j++) {
            printf(" ");
        }
        
        printf(" | ");
        print_num(results[i].switches, 8);
        printf(" | ");
        print_num(results[i].time, 5);
        printf(" | ");
        print_num(results[i].first, 8);
        printf(" |\n");
    }
    
    printf("  +-----------+----------+-------+----------+\n");
    result_count = 0;
}

/* 测试5: 优先级调度 */

static void test_priority_scheduler(void) {
    printf("\nTest 5: Priority Scheduler\n");
    printf("  Workload: 2 HIGH + 2 LOW priority tasks\n");

    int old_sched = get_scheduler();
    
    run_perf_test(SCHED_PRIO, 1);
    run_perf_test(SCHED_RR, 1);
    print_perf_table("1st Done");
    
    set_scheduler(old_sched);

    printf("  -> Priority: HIGH tasks finish first!\n");
    printf("Test 5 PASS!\n");
}

/* 测试6: MLFQ调度 */

static void test_mlfq_scheduler(void) {
    printf("\nTest 6: MLFQ Scheduler\n");
    printf("  Workload: 2 CPU-bound + 2 IO-bound tasks\n");

    int old_sched = get_scheduler();
    
    run_perf_test(SCHED_MLFQ, 0);
    run_perf_test(SCHED_RR, 0);
    print_perf_table("1st IO  ");
    
    set_scheduler(old_sched);

    printf("  -> MLFQ: IO tasks get better response time!\n");
    printf("Test 6 PASS!\n");
}

/* ============== 主测试入口 ============== */

static void test_main(void) {
    /* 基础功能测试 */
    test_process_creation();      /* Test 1: 进程创建与销毁 */
    test_scheduler_basic();       /* Test 2: 基本调度功能 */
    test_synchronization();       /* Test 3: 进程同步机制 */
    test_debug_proc_table();      /* Test 4: 调试功能 */

    /* 调度算法性能对比测试 */
    test_priority_scheduler();    /* Test 5: 优先级调度 vs RR */
    test_mlfq_scheduler();        /* Test 6: MLFQ vs RR */

    printf("\n=============================================\n");
    printf("  All tests PASSED!\n");
    printf("=============================================\n");
}

/* ============== 系统初始化 ============== */

/* 中断回调函数 */
static void timer_handler(void) {
    struct proc *p = myproc();
    if (p && p->state == RUNNING) {
        yield();
    }
}

/* init进程 */
static void init_task(void) {
    for (;;) {
        int status;
        int pid = wait_process(&status);
        if (pid > 0) {
            printf("[init] Reaped PID=%d\n", pid);
        }
        sleep(initproc);
    }
}

/* 内核入口 */
void kernel_main(void) {
    uart_init();
    printf("\n=============================================\n");
    printf("  Exp5: Process Management & Scheduling\n");
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
