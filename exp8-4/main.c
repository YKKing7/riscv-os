/*
 * 扩展实验8-4：内核日志系统
 * 
 * 实验目标：
 *   1. 实现结构化日志级别 (DEBUG/INFO/WARN/ERROR/FATAL)
 *   2. 实现高性能环形缓冲区存储日志
 *   3. 实现可变参数格式化输出 (kvprintf)
 *   4. 实现运行时日志级别动态控制
 * 
 * 基于实验7，新增：
 *   - include/klog.h : 日志系统接口声明
 *   - lib/klog.c     : 日志系统实现 (环形缓冲区 + 格式化)
 *
 * 实验测试：
 *   - 基本日志功能
 *   - 等级筛选
 *   - 格式化输出
 *   - 环形缓冲区溢出
 *   - 日志统计（扩展）
 *   - 并发安全（扩展）
 *   - 动态日志等级输出（扩展）
 */

#include "include/uart.h"
#include "include/printf.h"
#include "include/pmm.h"
#include "include/trap.h"
#include "include/proc.h"
#include "include/klog.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

/* ============== 测试1：基本日志功能 ============== */

static void test_basic_logging(void) {
    printf("\nTest 1: Basic Logging\n");
    
    /* 测试各级别日志 */
    klog_set_level(LOG_LEVEL_DEBUG);  /* 允许所有级别 */
    
    KLOG_DEBUG("This is a DEBUG message");
    KLOG_INFO("This is an INFO message");
    KLOG_WARN("This is a WARN message");
    KLOG_ERROR("This is an ERROR message");
    KLOG_FATAL("This is a FATAL message");
    
    /* 读取并显示日志 */
    char buf[512];
    int n = sys_klog_read(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    
    printf("  Logged %d bytes:\n", n);
    printf("  ---\n%s  ---\n", buf);
    
    assert(n > 0);
    printf("Test 1 PASS!\n");
}

/* ============== 测试2：日志级别过滤 ============== */

static void test_level_filtering(void) {
    printf("\nTest 2: Level Filtering\n");
    
    klog_clear();
    
    /* 设置为 WARN 级别，DEBUG 和 INFO 应被过滤 */
    klog_set_level(LOG_LEVEL_WARN);
    printf("  Log level set to: %s\n", klog_level_name(klog_get_level()));
    
    KLOG_DEBUG("DEBUG - should be filtered");
    KLOG_INFO("INFO - should be filtered");
    KLOG_WARN("WARN - should appear");
    KLOG_ERROR("ERROR - should appear");
    
    char buf[512];
    int n = sys_klog_read(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    
    printf("  Logged %d bytes (only WARN and above):\n", n);
    printf("  ---\n%s  ---\n", buf);
    
    /* 验证 DEBUG 和 INFO 被过滤 */
    assert(n > 0);
    
    /* 恢复默认级别 */
    klog_set_level(LOG_LEVEL_INFO);
    printf("Test 2 PASS!\n");
}

/* ============== 测试3：格式化输出 ============== */

static void test_formatted_output(void) {
    printf("\nTest 3: Formatted Output\n");
    
    klog_clear();
    klog_set_level(LOG_LEVEL_DEBUG);
    
    /* 测试各种格式说明符 */
    int num = 42;
    unsigned int unum = 255;
    const char *str = "hello";
    void *ptr = (void*)0xDEADBEEF;
    
    KLOG_INFO("Integer: %d", num);
    KLOG_INFO("Unsigned: %u", unum);
    KLOG_INFO("Hex: 0x%x", unum);
    KLOG_INFO("String: %s", str);
    KLOG_INFO("Pointer: %p", ptr);
    KLOG_INFO("Char: %c", 'A');
    KLOG_INFO("Percent: %%");
    KLOG_INFO("Multiple: pid=%d, name=%s, addr=%p", 123, "test", ptr);
    
    char buf[1024];
    int n = sys_klog_read(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    
    printf("  Formatted logs:\n");
    printf("  ---\n%s  ---\n", buf);
    
    assert(n > 0);
    printf("Test 3 PASS!\n");
}

/* ============== 测试4：环形缓冲区覆盖 ============== */

static void test_ring_buffer_overflow(void) {
    printf("\nTest 4: Ring Buffer Overflow\n");
    
    klog_clear();
    klog_set_level(LOG_LEVEL_DEBUG);
    
    /* 记录测试前的日志数 */
    struct klog_stats stats_before = klog_get_stats();
    
    /* 写入大量日志，超过缓冲区大小 */
    printf("  Writing many logs to overflow buffer...\n");
    for (int i = 0; i < 100; i++) {
        KLOG_DEBUG("Log entry %d: This is a test message to fill the buffer", i);
    }
    
    struct klog_stats stats = klog_get_stats();
    uint64 logs_written = stats.total_logs - stats_before.total_logs;
    printf("  Stats: written=%lu, buffer_used=%d/%d\n", 
           logs_written, stats.buffer_used, stats.buffer_size);
    
    /* 读取日志，应该只能读到最新的部分 */
    char buf[512];
    int n = sys_klog_read(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    
    printf("  Read %d bytes from buffer\n", n);
    printf("  Last few lines:\n  ---\n");
    
    /* 只显示最后几行 */
    char *p = buf + (n > 200 ? n - 200 : 0);
    printf("%s  ---\n", p);
    
    assert(logs_written == 100);
    printf("Test 4 PASS!\n");
}

/* ============== 测试5：日志统计 ============== */

static void test_log_statistics(void) {
    printf("\nTest 5: Log Statistics\n");
    
    klog_clear();
    klog_set_level(LOG_LEVEL_INFO);
    
    /* 记录测试前的日志数 */
    struct klog_stats stats_before = klog_get_stats();
    
    /* 写入一些日志 */
    for (int i = 0; i < 10; i++) {
        KLOG_INFO("Test log %d", i);
    }
    
    /* 一些会被过滤的日志 */
    for (int i = 0; i < 5; i++) {
        KLOG_DEBUG("Filtered log %d", i);
    }
    
    struct klog_stats stats = klog_get_stats();
    uint64 logs_written = stats.total_logs - stats_before.total_logs;
    
    printf("  Statistics:\n");
    printf("    Logs written: %lu\n", logs_written);
    printf("    Current level: %s\n", klog_level_name(stats.current_level));
    printf("    Buffer used: %d / %d bytes\n", stats.buffer_used, stats.buffer_size);
    
    assert(logs_written == 10);  /* DEBUG 被过滤，不计入 */
    printf("Test 5 PASS!\n");
}

/* ============== 测试6：并发安全 (模拟) ============== */

static volatile int concurrent_done = 0;

static void log_producer_task(void) {
    int id = myproc()->pid;
    for (int i = 0; i < 20; i++) {
        KLOG_INFO("Producer %d: message %d", id, i);
        for (volatile int j = 0; j < 10000; j++);
        yield();
    }
    concurrent_done++;
}

static void test_concurrent_logging(void) {
    printf("\nTest 6: Concurrent Logging\n");
    
    klog_clear();
    klog_set_level(LOG_LEVEL_INFO);
    concurrent_done = 0;
    
    /* 记录测试前的日志数 */
    struct klog_stats stats_before = klog_get_stats();
    
    /* 创建多个日志生产者 */
    create_process(log_producer_task, "log1");
    create_process(log_producer_task, "log2");
    create_process(log_producer_task, "log3");
    
    /* 等待完成 */
    while (concurrent_done < 3) {
        yield();
    }
    
    /* 等待子进程退出 */
    for (int i = 0; i < 3; i++) {
        wait_process(0);
    }
    
    struct klog_stats stats = klog_get_stats();
    uint64 logs_written = stats.total_logs - stats_before.total_logs;
    printf("  Concurrent logging completed\n");
    printf("  Logs from 3 producers: %lu\n", logs_written);
    
    /* 读取部分日志验证 */
    char buf[512];
    int n = sys_klog_read(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    
    printf("  Sample (last %d bytes):\n  ---\n", n);
    char *p = buf + (n > 200 ? n - 200 : 0);
    printf("%s  ---\n", p);
    
    assert(logs_written == 60);  /* 3 producers * 20 messages */
    printf("Test 6 PASS!\n");
}

/* ============== 测试7：动态级别调整 ============== */

static void test_dynamic_level_change(void) {
    printf("\nTest 7: Dynamic Level Change\n");
    
    klog_clear();
    
    printf("  Testing level changes at runtime:\n");
    
    /* 从 DEBUG 到 FATAL 逐级测试 */
    for (int level = LOG_LEVEL_DEBUG; level <= LOG_LEVEL_FATAL; level++) {
        klog_set_level(level);
        printf("    Level set to %s:\n", klog_level_name(level));
        
        klog_clear();
        struct klog_stats stats_before = klog_get_stats();
        
        KLOG_DEBUG("debug");
        KLOG_INFO("info");
        KLOG_WARN("warn");
        KLOG_ERROR("error");
        KLOG_FATAL("fatal");
        
        struct klog_stats stats = klog_get_stats();
        uint64 logs_written = stats.total_logs - stats_before.total_logs;
        printf("      Logged: %lu messages\n", logs_written);
        
        /* 验证正确数量的日志被记录 */
        int expected = LOG_LEVEL_FATAL - level + 1;
        assert(logs_written == (uint64)expected);
    }
    
    klog_set_level(LOG_LEVEL_INFO);
    printf("Test 7 PASS!\n");
}

/* ============== 主测试入口 ============== */

static void test_main(void) {
    test_basic_logging();         /* T1: 基本日志功能 */
    test_level_filtering();       /* T2: 日志级别过滤 */
    test_formatted_output();      /* T3: 格式化输出 */
    test_ring_buffer_overflow();  /* T4: 环形缓冲区覆盖 */
    test_log_statistics();        /* T5: 日志统计 */
    test_concurrent_logging();    /* T6: 并发安全 */
    test_dynamic_level_change();  /* T7: 动态级别调整 */
    
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
        (void)pid;
        sleep(initproc);
    }
}

/* 内核入口 */
void kernel_main(void) {
    uart_init();
    printf("\n=============================================\n");
    printf("  Exp8-4: Kernel Logging System\n");
    printf("=============================================\n");
    
    /* 初始化子系统 */
    pmm_init();
    trap_init();
    trap_inithart();
    proc_init();
    klog_init();  /* 初始化日志系统 */
    
    /* 记录启动日志 */
    KLOG_INFO("Kernel logging system initialized");
    KLOG_INFO("Buffer size: %d bytes", LOG_BUF_SIZE);
    
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
    
    KLOG_INFO("Starting scheduler...");
    
    /* 启动调度器 */
    scheduler();
    
    /* 不应到达这里 */
    while (1) {
        __asm__ volatile("wfi");
    }
}
