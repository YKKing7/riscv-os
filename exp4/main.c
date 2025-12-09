/*
 * 实验4：中断与异常处理
 * 
 * 实验目标：
 *   1. 理解 RISC-V 中断/异常机制
 *   2. 实现中断处理框架 (注册/分发)
 *   3. 实现时钟中断处理
 *   4. 实现异常处理 (非法指令等)
 *   5. 中断优先级管理
 *   6. 共享中断支持
 *   7. 中断嵌套支持
 * 
 * 基于实验3，新增：
 *   - include/trap.h  : 中断/异常接口声明
 *   - trap/trap.c     : 中断/异常处理实现
 *   - trap/trapvec.S  : 中断入口汇编
 */

#include "include/uart.h"
#include "include/printf.h"
#include "include/pmm.h"
#include "include/trap.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

/* ============== 测试状态变量 ============== */

static volatile int interrupt_count = 0;        /* 中断计数器 */
static volatile int exception_handled = 0;      /* 异常处理标志 */
static volatile uint64 exception_cause = 0;     /* 异常原因 */
static volatile int shared_handler1_count = 0;  /* 共享处理函数1计数 */
static volatile int shared_handler2_count = 0;  /* 共享处理函数2计数 */
static volatile int nesting_max_depth = 0;      /* 最大嵌套深度 */
static volatile int nesting_high_count = 0;     /* 高优先级中断计数 */
static volatile int nesting_low_count = 0;      /* 低优先级中断计数 */

/* ============== 中断回调函数 ============== */

static void timer_handler(void) {
    interrupt_count++;
}

static void illegal_inst_handler(struct trapframe *tf) {
    exception_handled = 1;
    exception_cause = tf->scause;
    tf->sepc += 4;  /* 跳过非法指令 */
}

static int shared_handler1(void *dev_id) {
    shared_handler1_count++;
    return 0;  /* 继续调用下一个处理函数 */
}

static int shared_handler2(void *dev_id) {
    shared_handler2_count++;
    return 0;
}

static void print_indent(int depth) {
    printf("    ");  /* 基础缩进 */
    for (int i = 1; i < depth; i++) printf("  ");
}

static void high_prio_handler(void) {
    int depth = get_irq_nesting_level();
    if (depth > nesting_max_depth) nesting_max_depth = depth;
    nesting_high_count++;
    print_indent(depth);
    printf("[HIGH] prio=1 depth=%d%s\n", depth, depth > 1 ? " (nested!)" : "");
}

static void low_prio_handler(void) {
    int depth = get_irq_nesting_level();
    if (depth > nesting_max_depth) nesting_max_depth = depth;
    nesting_low_count++;
    print_indent(depth);
    printf("[LOW]  prio=3 depth=%d start\n", depth);
    
    /* 只在第一层嵌套时等待下一次中断，避免无限嵌套 */
    if (depth == 1) {
        uint64 start_tick = get_ticks();
        while (get_ticks() == start_tick) {
            /* 等待时钟中断发生，高优先级中断将抢占 */
        }
    }
    
    print_indent(depth);
    printf("[LOW]  prio=3 depth=%d end\n", depth);
}

/* ============== 测试函数 ============== */

/* 测试1：时钟中断 - 验证时钟中断能正确触发和处理 */
static void test_timer_interrupt(void) {
    printf("\nTest 1: Timer Interrupt\n");
    
    register_interrupt_handler(IRQ_S_TIMER, timer_handler);
    interrupt_count = 0;
    uint64 start = get_time();
    
    printf("  Waiting for 5 interrupts...\n");
    while (interrupt_count < 5) {
        for (volatile int i = 0; i < 1000000; i++);
    }
    
    uint64 elapsed = get_time() - start;
    printf("  Received %d interrupts in %lu cycles\n", interrupt_count, elapsed);
    assert(interrupt_count >= 5);
    printf("Test 1 PASS!\n");
}

/* 测试2：异常处理 - 验证异常框架和非法指令异常 */
static void test_exception_handling(void) {
    printf("\nTest 2: Exception Handling\n");
    
    printf("  stvec register... ");
    assert(r_stvec() != 0);
    printf("0x%lx OK\n", r_stvec());
    
    printf("  sie register... ");
    assert(r_sie() != 0);
    printf("0x%lx OK\n", r_sie());
    
    printf("  Illegal instruction exception... ");
    exception_handled = 0;
    register_exception_handler(EXC_ILLEGAL_INST, illegal_inst_handler);
    __asm__ volatile(".word 0x00000000");
    assert(exception_handled == 1);
    printf("caught & handled OK\n");
    
    printf("Test 2 PASS!\n");
}

/* 测试3：中断延迟 - 测量时钟中断间隔 */
static void test_interrupt_latency(void) {
    printf("\nTest 3: Interrupt Latency\n");
    
    uint64 start = get_time();
    uint64 start_ticks = get_ticks();
    int count = 10;
    
    while (get_ticks() < start_ticks + count) __asm__ volatile("wfi");
    
    uint64 elapsed = get_time() - start;
    printf("  %d ticks, %lu cycles total\n", count, elapsed);
    printf("  Average interval: %lu cycles (~%lu us @100MHz)\n", 
           elapsed / count, elapsed / count / 100);
    assert(elapsed > 0);
    printf("Test 3 PASS!\n");
}

/* 测试4：中断优先级 - 验证优先级设置和获取 */
static void test_interrupt_priority(void) {
    printf("\nTest 4: Interrupt Priority\n");
    printf("  Levels: 0=HIGHEST 1=HIGH 2=NORMAL 3=LOW 4=LOWEST\n");
    
    set_irq_priority(IRQ_S_TIMER, IRQ_PRIO_HIGH);
    printf("  set_irq_priority(TIMER, HIGH): %d... ", get_irq_priority(IRQ_S_TIMER));
    assert(get_irq_priority(IRQ_S_TIMER) == IRQ_PRIO_HIGH);
    printf("OK\n");
    
    register_interrupt_handler_prio(IRQ_S_TIMER, timer_handler, IRQ_PRIO_HIGHEST);
    printf("  register with HIGHEST: %d... ", get_irq_priority(IRQ_S_TIMER));
    assert(get_irq_priority(IRQ_S_TIMER) == IRQ_PRIO_HIGHEST);
    printf("OK\n");
    
    printf("  Nesting level (outside IRQ): %d\n", get_irq_nesting_level());
    printf("Test 4 PASS!\n");
}

/* 测试5：共享中断 - 多个设备共享同一中断线 */
static void test_shared_interrupt(void) {
    printf("\nTest 5: Shared Interrupt\n");
    
    shared_handler1_count = shared_handler2_count = 0;
    
    printf("  Register 2 handlers on Timer IRQ...\n");
    int r1 = request_shared_irq(IRQ_S_TIMER, shared_handler1, (void*)1, "dev1");
    int r2 = request_shared_irq(IRQ_S_TIMER, shared_handler2, (void*)2, "dev2");
    assert(r1 == 0 && r2 == 0);
    printf("  Registered OK\n");
    
    uint64 start = get_ticks();
    while (get_ticks() < start + 3) __asm__ volatile("wfi");
    
    printf("  Handler1 called %d times, Handler2 called %d times\n", 
           shared_handler1_count, shared_handler2_count);
    assert(shared_handler1_count > 0 && shared_handler2_count > 0);
    
    free_shared_irq(IRQ_S_TIMER, (void*)1);
    free_shared_irq(IRQ_S_TIMER, (void*)2);
    printf("Test 5 PASS!\n");
}

/* 测试6：中断嵌套 - 高优先级中断可抢占低优先级 */
static void test_interrupt_nesting(void) {
    printf("\nTest 6: Interrupt Nesting\n");
    printf("  Timer=LOW(3), Soft=HIGH(1), high can preempt low\n");
    
    nesting_max_depth = nesting_high_count = nesting_low_count = 0;
    register_interrupt_handler_prio(IRQ_S_TIMER, low_prio_handler, IRQ_PRIO_LOW);
    register_interrupt_handler_prio(IRQ_S_SOFT, high_prio_handler, IRQ_PRIO_HIGH);
    
    printf("  Trace:\n");
    uint64 start = get_ticks();
    while (get_ticks() < start + 3) __asm__ volatile("wfi");
    
    printf("  Result: max_depth=%d, high=%d, low=%d\n", 
           nesting_max_depth, nesting_high_count, nesting_low_count);
    assert(nesting_low_count > 0 && nesting_high_count > 0);
    
    /* 恢复默认设置 */
    register_interrupt_handler_prio(IRQ_S_TIMER, timer_handler, IRQ_PRIO_NORMAL);
    register_interrupt_handler(IRQ_S_SOFT, 0);  /* 清除软件中断处理函数 */
    printf("Test 6 PASS!\n");
}

/* ============== 内核主函数 ============== */

void kernel_main(void) {
    uart_init();
    printf("\n=============================================\n");
    printf("  Exp4: Interrupt & Exception Handling\n");
    printf("=============================================\n");
    
    pmm_init();
    trap_init();
    trap_inithart();
    
    test_timer_interrupt();
    test_exception_handling();
    test_interrupt_latency();
    test_interrupt_priority();
    test_shared_interrupt();
    test_interrupt_nesting();
    
    printf("\n=============================================\n");
    printf("  All tests passed!\n");
    printf("=============================================\n");
    
    while (1) __asm__ volatile("wfi");
}
