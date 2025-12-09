/*
 * 实验6：系统调用
 * 
 * 实验目标：
 *   1. 系统调用框架实现 (分发/参数提取)
 *   2. 进程控制类系统调用 (fork, exit, wait, kill, getpid)
 *   3. 文件操作类系统调用 (read, write, open, close)
 *   4. 内存映射系统调用 (mmap, munmap)
 *   5. 系统调用安全性检查
 *   6. 性能优化: 批量系统调用 (sys_batch)
 * 
 * 基于实验5，新增：
 *   - include/syscall.h : 系统调用接口定义
 *   - syscall/syscall.c : 系统调用分发机制
 *   - syscall/sysproc.c : 进程系统调用实现
 *   - syscall/sysfile.c : 文件系统调用实现
 *
 * 实验测试：
 *   - 基础功能测试
 *   - 参数传递测试
 *   - 安全性测试
 *   - 性能测试
 *   - mmap/munmap（扩展）
 *   - 批处理优化（扩展）
 */

#include "include/uart.h"
#include "include/printf.h"
#include "include/pmm.h"
#include "include/trap.h"
#include "include/proc.h"
#include "include/syscall.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

/* ============== 基础功能测试辅助任务 ============== */

static void child_task(void) {
    printf("  Child process: PID=%d\n", myproc()->pid);
    exit_process(42);
}

/* ============== 基础功能测试：test_basic_syscalls ============== */

static void test_basic_syscalls(void) {
    printf("\nTest 1: Basic System Calls\n");
    printf("Testing basic system calls...\n");
    
    struct proc *p = myproc();
    
    /* 测试 getpid */
    int pid = p->pid;
    if (p->trapframe) {
        p->trapframe->a7 = SYS_getpid;
        syscall();
        int result = (int)p->trapframe->a0;
        printf("  Current PID: %d\n", result);
        assert(result == pid);
    }
    
    /* 测试 fork (通过 create_process 模拟) */
    int child_pid = create_process(child_task, "child");
    if (child_pid > 0) {
        /* 父进程 */
        int status;
        wait_process(&status);
        printf("  Child exited with status: %d\n", status);
        assert(status == 42);
    } else {
        printf("  Fork failed!\n");
        assert(0);
    }
    
    printf("Test 1 PASS!\n");
}

/* ============== 参数传递测试：test_parameter_passing ============== */

static void test_parameter_passing(void) {
    printf("\nTest 2: Parameter Passing\n");
    
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        printf("  Skipped (no trapframe)\n");
        return;
    }
    
    /* 测试不同类型参数的传递 */
    char buffer[] = "Hello, World!";
    
    /* 测试 write 系统调用 - 整数和指针参数 */
    printf("  Testing write syscall...\n");
    
    /* 禁用中断以防止 trapframe 被修改 */
    push_off();
    
    p->trapframe->a7 = SYS_write;
    p->trapframe->a0 = 1;  /* fd: stdout */
    p->trapframe->a1 = (uint64)buffer;
    p->trapframe->a2 = 13;  /* count */
    
    syscall();
    int bytes_written = (int)p->trapframe->a0;
    
    pop_off();
    /* 中断已恢复 */
    
    printf("  Wrote %d bytes\n", bytes_written);
    assert(bytes_written == 13);
    
    /* 测试边界情况：无效文件描述符 */
    p->trapframe->a7 = SYS_write;
    p->trapframe->a0 = -1;  /* invalid fd */
    p->trapframe->a1 = (uint64)buffer;
    p->trapframe->a2 = 10;
    
    syscall();
    int result = (int)p->trapframe->a0;
    printf("  write(-1, buffer, 10) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试边界情况：负数长度 */
    p->trapframe->a7 = SYS_write;
    p->trapframe->a0 = 1;
    p->trapframe->a1 = (uint64)buffer;
    p->trapframe->a2 = -1;  /* negative count */
    
    syscall();
    result = (int)p->trapframe->a0;
    printf("  write(1, buffer, -1) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试 sbrk 系统调用 - 整数参数 */
    p->trapframe->a7 = SYS_sbrk;
    p->trapframe->a0 = 4096;
    
    syscall();
    uint64 old_brk = p->trapframe->a0;
    printf("  sbrk(4096) = 0x%lx\n", old_brk);
    
    p->trapframe->a7 = SYS_sbrk;
    p->trapframe->a0 = 0;
    
    syscall();
    uint64 new_brk = p->trapframe->a0;
    printf("  sbrk(0) = 0x%lx\n", new_brk);
    assert(new_brk == old_brk + 4096);
    
    printf("Test 2 PASS!\n");
}

/* ============== 安全性测试：test_security ============== */

static void test_security(void) {
    printf("\nTest 3: Security\n");
    
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        printf("  Skipped (no trapframe)\n");
        return;
    }
    
    /* 测试无效指针访问 */
    printf("  Testing invalid pointer detection...\n");
    int result;
    
    /* 测试空指针检测 */
    result = check_user_ptr((void*)0, 10);
    printf("  check_user_ptr(NULL, 10) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试地址溢出检测 */
    result = check_user_ptr((void*)0xFFFFFFFFFFFFFFFFUL, 10);
    printf("  check_user_ptr(0xFFFF..., 10) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试 write 系统调用使用空指针 */
    p->trapframe->a7 = SYS_write;
    p->trapframe->a0 = 1;       /* fd: stdout */
    p->trapframe->a1 = 0;       /* NULL buffer */
    p->trapframe->a2 = 10;      /* count */
    syscall();
    result = (int)p->trapframe->a0;
    printf("  write(1, NULL, 10) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试 read 系统调用使用空指针 */
    p->trapframe->a7 = SYS_read;
    p->trapframe->a0 = 0;       /* fd: stdin */
    p->trapframe->a1 = 0;       /* NULL buffer */
    p->trapframe->a2 = 10;      /* count */
    syscall();
    result = (int)p->trapframe->a0;
    printf("  read(0, NULL, 10) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试无效系统调用号 */
    p->trapframe->a7 = 999;
    syscall();
    result = (int)p->trapframe->a0;
    printf("  syscall(999) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试负数系统调用号 */
    p->trapframe->a7 = -1;
    syscall();
    result = (int)p->trapframe->a0;
    printf("  syscall(-1) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 测试 kill 不存在的进程 */
    result = kill(9999);
    printf("  kill(9999) = %d (expected -1)\n", result);
    assert(result == -1);
    
    printf("Test 3 PASS!\n");
}

/* ============== 性能测试：test_syscall_performance ============== */

static void test_syscall_performance(void) {
    printf("\nTest 4: Syscall Performance\n");
    
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        printf("  Skipped (no trapframe)\n");
        return;
    }
    
    uint64 start_time = get_time();
    
    /* 大量系统调用测试 */
    for (int i = 0; i < 10000; i++) {
        p->trapframe->a7 = SYS_getpid;
        syscall();
    }
    
    uint64 end_time = get_time();
    uint64 elapsed = end_time - start_time;
    
    printf("  10000 getpid() calls took %lu cycles\n", elapsed);
    printf("  Average: %lu cycles/call\n", elapsed / 10000);
    
    printf("Test 4 PASS!\n");
}

/* ============== mmap/munmap 测试：test_mmap ============== */

static void test_mmap(void) {
    printf("\nTest 5: mmap/munmap\n");
    
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        printf("  Skipped (no trapframe)\n");
        return;
    }
    
    /* 测试1: 基本匿名映射 */
    printf("  Testing anonymous mmap...\n");
    p->trapframe->a7 = SYS_mmap;
    p->trapframe->a0 = 0;                           /* addr: 内核选择 */
    p->trapframe->a1 = 4096;                        /* length: 1页 */
    p->trapframe->a2 = PROT_READ | PROT_WRITE;      /* prot */
    p->trapframe->a3 = MAP_PRIVATE | MAP_ANONYMOUS; /* flags */
    p->trapframe->a4 = (uint64)-1;                  /* fd: 匿名映射 */
    p->trapframe->a5 = 0;                           /* offset */
    syscall();
    
    uint64 addr1 = p->trapframe->a0;
    printf("  mmap(0, 4096, RW, ANON) = 0x%lx\n", addr1);
    assert(addr1 != (uint64)-1);
    
    /* 验证内存已清零 */
    char *mem = (char*)addr1;
    int zero_ok = 1;
    for (int i = 0; i < 100; i++) {
        if (mem[i] != 0) { zero_ok = 0; break; }
    }
    printf("  Memory zeroed: %s\n", zero_ok ? "YES" : "NO");
    assert(zero_ok);
    
    /* 测试读写 */
    mem[0] = 'H';
    mem[1] = 'i';
    mem[2] = '\0';
    printf("  Write test: \"%s\"\n", mem);
    assert(mem[0] == 'H' && mem[1] == 'i');
    
    /* 测试2: 多次映射 */
    printf("  Testing multiple mappings...\n");
    p->trapframe->a7 = SYS_mmap;
    p->trapframe->a0 = 0;
    p->trapframe->a1 = 8192;  /* 2页 */
    p->trapframe->a2 = PROT_READ | PROT_WRITE;
    p->trapframe->a3 = MAP_PRIVATE | MAP_ANONYMOUS;
    p->trapframe->a4 = -1;
    p->trapframe->a5 = 0;
    syscall();
    
    uint64 addr2 = p->trapframe->a0;
    printf("  mmap(0, 8192, RW, ANON) = 0x%lx\n", addr2);
    assert(addr2 != (uint64)-1);
    assert(addr2 != addr1);  /* 不应重叠 */
    
    /* 测试3: munmap */
    printf("  Testing munmap...\n");
    p->trapframe->a7 = SYS_munmap;
    p->trapframe->a0 = addr1;
    p->trapframe->a1 = 4096;
    syscall();
    
    int result = (int)p->trapframe->a0;
    printf("  munmap(0x%lx, 4096) = %d\n", addr1, result);
    assert(result == 0);
    
    /* 测试4: 错误情况 */
    printf("  Testing error cases...\n");
    
    /* 无效长度 */
    p->trapframe->a7 = SYS_mmap;
    p->trapframe->a0 = 0;
    p->trapframe->a1 = 0;  /* 无效长度 */
    p->trapframe->a2 = PROT_READ;
    p->trapframe->a3 = MAP_PRIVATE | MAP_ANONYMOUS;
    p->trapframe->a4 = -1;
    p->trapframe->a5 = 0;
    syscall();
    result = (int)p->trapframe->a0;
    printf("  mmap(0, 0, ...) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* munmap 未映射的地址 */
    p->trapframe->a7 = SYS_munmap;
    p->trapframe->a0 = 0x12340000;  /* 未映射的地址 */
    p->trapframe->a1 = 4096;
    syscall();
    result = (int)p->trapframe->a0;
    printf("  munmap(0x12340000, 4096) = %d (expected -1)\n", result);
    assert(result == -1);
    
    /* 清理第二个映射 */
    p->trapframe->a7 = SYS_munmap;
    p->trapframe->a0 = addr2;
    p->trapframe->a1 = 8192;
    syscall();
    
    printf("Test 5 PASS!\n");
}

/* ============== 性能优化测试：test_performance_optimization ============== */

/* 批量系统调用请求结构 */
struct batch_request {
    int64 syscall_num;
    uint64 args[6];
    int64 result;
};

static void test_performance_optimization(void) {
    printf("\nTest 6: Performance Optimization\n");
    
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        printf("  Skipped (no trapframe)\n");
        return;
    }
    
    struct batch_request reqs[BATCH_MAX];
    const int N = 1000, B = 8;  /* N次迭代, 每批B个调用 */
    uint64 t0, t1, t2;
    
    /* 传统方式: N*B 次单独调用 */
    printf("  Individual vs Batch (N=%d, B=%d):\n", N, B);
    t0 = get_time();
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < B; j++) {
            p->trapframe->a7 = SYS_getpid;
            syscall();
        }
    }
    t1 = get_time();
    
    /* 批量方式: N 次批量调用 */
    for (int i = 0; i < B; i++) {
        reqs[i].syscall_num = SYS_getpid;
        for (int j = 0; j < 6; j++) reqs[i].args[j] = 0;
    }
    for (int i = 0; i < N; i++) {
        p->trapframe->a7 = SYS_batch;
        p->trapframe->a0 = (uint64)reqs;
        p->trapframe->a1 = B;
        syscall();
    }
    t2 = get_time();
    
    uint64 ind = t1 - t0, bat = t2 - t1;
    printf("    Individual: %lu cycles\n", ind);
    printf("    Batch:      %lu cycles\n", bat);
    
    /* 验证批量调用成功 */
    int ok = (int)p->trapframe->a0 == B;
    printf("    Result: %s\n", ok ? "OK" : "FAIL");
    assert(ok);
    
    if (bat < ind) {
        printf("    Speedup: %lu%%\n", ((ind - bat) * 100) / ind);
    }
    
    printf("Test 6 PASS!\n");
}

/* ============== 主测试 ============== */

static void test_main(void) {
    
    test_basic_syscalls();
    test_parameter_passing();
    test_security();
    test_syscall_performance();
    test_mmap();
    test_performance_optimization();
    
    printf("\n=============================================\n");
    printf("  All tests passed!\n");
    printf("=============================================\n");
}

/* ============== 系统初始化 ============== */

static void timer_handler(void) {
    struct proc *p = myproc();
    if (p && p->state == RUNNING) yield();
}

static void init_task(void) {
    for (;;) {
        int status, pid = wait_process(&status);
        if (pid > 0) printf("[init] Reaped PID=%d\n", pid);
        sleep(initproc);
    }
}

void kernel_main(void) {
    uart_init();
    printf("\n=============================================\n");
    printf("  Exp6: System Calls\n");
    printf("=============================================\n");
    
    pmm_init();
    trap_init();
    trap_inithart();
    proc_init();
    syscall_init();
    
    /* 创建 init 进程 */
    int init_pid = create_process(init_task, "init");
    assert(init_pid > 0);
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].pid == init_pid) { initproc = &proc[i]; break; }
    }
    assert(initproc != 0);
    
    register_interrupt_handler(IRQ_S_TIMER, timer_handler);
    
    int test_pid = create_process(test_main, "test_main");
    assert(test_pid > 0);
    
    scheduler();
    while (1) __asm__ volatile("wfi");
}
