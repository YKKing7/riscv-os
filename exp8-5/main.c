/*
 * 扩展实验8-5：写时复制 (Copy-on-Write) Fork
 * 
 * 实验目标：
 *   1. 理解 COW 的核心思想：懒分配与延迟复制
 *   2. 实现物理页引用计数管理
 *   3. 实现 COW 页面标记与缺页处理
 *   4. 验证 COW 的内存效率优势
 * 
 * 基于实验7，新增：
 *   - include/cow.h  : COW 系统接口声明
 *   - mm/cow.c       : COW 实现 (引用计数 + 缺页处理)
 *   - include/riscv.h: 添加 PTE_C 标志位
 *
 * 实验测试：
 *   - 引用计数功能
 *   - 多页引用计数
 *   - 模拟COW Fork
 *   - fork后大量写内存性能测试
 *   - fork不写内存效率演示
 */

#include "include/types.h"
#include "include/riscv.h"
#include "include/printf.h"
#include "include/proc.h"
#include "include/pmm.h"
#include "include/cow.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

/* ============== 测试1：引用计数基本功能 ============== */

static void test_refcount_basic(void) {
    printf("\nTest 1: Reference Count Basic\n");
    
    /* 分配一个页面 */
    void *page = alloc_page();
    assert(page != 0);
    
    uint64 pa = (uint64)page;
    
    /* 初始引用计数应为 0 */
    int ref = page_getref(pa);
    printf("  Initial refcount: %d\n", ref);
    
    /* 增加引用 */
    page_incref(pa);
    ref = page_getref(pa);
    printf("  After incref: %d\n", ref);
    assert(ref == 1);
    
    /* 再次增加 */
    page_incref(pa);
    ref = page_getref(pa);
    printf("  After second incref: %d\n", ref);
    assert(ref == 2);
    
    /* 减少引用 */
    int newref = page_decref(pa);
    printf("  After decref: %d\n", newref);
    assert(newref == 1);
    
    /* 再次减少 */
    newref = page_decref(pa);
    printf("  After second decref: %d\n", newref);
    assert(newref == 0);
    
    /* 释放页面 */
    free_page(page);
    
    printf("Test 1 PASS!\n");
}

/* ============== 测试2：多页引用计数 ============== */

static void test_multiple_pages(void) {
    printf("\nTest 2: Multiple Pages Reference Count\n");
    
    void *pages[5];
    
    /* 分配多个页面 */
    for (int i = 0; i < 5; i++) {
        pages[i] = alloc_page();
        assert(pages[i] != 0);
        page_incref((uint64)pages[i]);
    }
    
    /* 验证每个页面引用计数为 1 */
    for (int i = 0; i < 5; i++) {
        int ref = page_getref((uint64)pages[i]);
        printf("  Page %d refcount: %d\n", i, ref);
        assert(ref == 1);
    }
    
    /* 模拟共享：增加所有页面引用 */
    for (int i = 0; i < 5; i++) {
        page_incref((uint64)pages[i]);
    }
    
    printf("  After sharing (all +1):\n");
    for (int i = 0; i < 5; i++) {
        int ref = page_getref((uint64)pages[i]);
        printf("    Page %d refcount: %d\n", i, ref);
        assert(ref == 2);
    }
    
    /* 释放一个引用 */
    for (int i = 0; i < 5; i++) {
        page_decref((uint64)pages[i]);
        page_decref((uint64)pages[i]);
        free_page(pages[i]);
    }
    
    printf("Test 2 PASS!\n");
}

/* ============== 测试3：模拟 COW Fork 场景 ============== */

static void test_cow_fork_simulation(void) {
    printf("\nTest 3: COW Fork Simulation\n");
    
    /* 分配一个"父进程"页面 */
    void *parent_page = alloc_page();
    assert(parent_page != 0);
    
    /* 写入数据 */
    int *data = (int*)parent_page;
    for (int i = 0; i < 1024; i++) {
        data[i] = i * 10;
    }
    printf("  Parent page allocated at 0x%lx\n", (uint64)parent_page);
    printf("  Parent data[0] = %d, data[100] = %d\n", data[0], data[100]);
    
    /* 模拟 fork: 增加引用计数 (父子共享) */
    page_incref((uint64)parent_page);  /* 父进程引用 */
    page_incref((uint64)parent_page);  /* 子进程引用 */
    
    int ref = page_getref((uint64)parent_page);
    printf("  After fork simulation, refcount: %d\n", ref);
    assert(ref == 2);
    
    /* 模拟子进程写入触发 COW */
    printf("  Simulating child write (COW trigger)...\n");
    
    /* 分配新页面给子进程 */
    void *child_page = alloc_page();
    assert(child_page != 0);
    
    /* 复制数据 */
    int *child_data = (int*)child_page;
    for (int i = 0; i < 1024; i++) {
        child_data[i] = data[i];
    }
    
    /* 子进程修改自己的副本 */
    child_data[0] = 9999;
    child_data[100] = 8888;
    
    /* 更新引用计数 */
    page_incref((uint64)child_page);   /* 子进程现在引用新页 */
    page_decref((uint64)parent_page);  /* 子进程不再引用父页 */
    
    printf("  Child page allocated at 0x%lx\n", (uint64)child_page);
    printf("  Child data[0] = %d, data[100] = %d\n", child_data[0], child_data[100]);
    printf("  Parent data[0] = %d, data[100] = %d (unchanged)\n", data[0], data[100]);
    
    /* 验证父进程数据未被修改 */
    assert(data[0] == 0);
    assert(data[100] == 1000);
    
    /* 验证子进程数据已修改 */
    assert(child_data[0] == 9999);
    assert(child_data[100] == 8888);
    
    /* 验证引用计数 */
    ref = page_getref((uint64)parent_page);
    printf("  Parent page refcount: %d\n", ref);
    assert(ref == 1);
    
    ref = page_getref((uint64)child_page);
    printf("  Child page refcount: %d\n", ref);
    assert(ref == 1);
    
    /* 清理 */
    page_decref((uint64)parent_page);
    page_decref((uint64)child_page);
    free_page(parent_page);
    free_page(child_page);
    
    printf("Test 3 PASS!\n");
}

/* ============== 测试4：fork后大量写内存性能测试 ============== */

static void test_cow_write_performance(void) {
    printf("\nTest 4: COW Write Performance (Page Fault Overhead)\n");
    
    /* 分配多个页面模拟进程内存 */
    #define NUM_PAGES 8
    void *pages[NUM_PAGES];
    
    printf("  Allocating %d pages for parent process...\n", NUM_PAGES);
    for (int i = 0; i < NUM_PAGES; i++) {
        pages[i] = alloc_page();
        assert(pages[i] != 0);
        page_incref((uint64)pages[i]);
        /* 初始化数据 */
        int *data = (int*)pages[i];
        data[0] = i * 100;
    }
    
    /* 模拟 fork: 子进程共享所有页面 */
    printf("  fork() - child shares all pages (COW)...\n");
    for (int i = 0; i < NUM_PAGES; i++) {
        page_incref((uint64)pages[i]);  /* 子进程引用 */
    }
    
    uint64 free_before_write = get_free_page_count();
    
    /* 模拟子进程写入所有页面，触发 COW */
    printf("  Child writes to all %d pages (triggering COW)...\n", NUM_PAGES);
    void *child_pages[NUM_PAGES];
    
    uint64 start_time = get_time();
    
    for (int i = 0; i < NUM_PAGES; i++) {
        /* COW: 分配新页面，复制数据 */
        child_pages[i] = alloc_page();
        assert(child_pages[i] != 0);
        
        /* 复制原页面内容 */
        char *src = (char*)pages[i];
        char *dst = (char*)child_pages[i];
        for (int j = 0; j < PGSIZE; j++) {
            dst[j] = src[j];
        }
        
        /* 修改数据 */
        int *data = (int*)child_pages[i];
        data[0] = i * 100 + 1;
        
        /* 更新引用计数 */
        page_incref((uint64)child_pages[i]);
        page_decref((uint64)pages[i]);
    }
    
    uint64 end_time = get_time();
    uint64 cow_time = end_time - start_time;
    
    /* 测量标准版本（直接写，无COW）的时间 */
    uint64 std_start = get_time();
    for (int i = 0; i < NUM_PAGES; i++) {
        int *data = (int*)child_pages[i];
        data[0] = i * 100 + 2;  /* 直接写，无需复制 */
    }
    uint64 std_time = get_time() - std_start;
    
    uint64 free_after_write = get_free_page_count();
    int pages_copied = (int)(free_before_write - free_after_write);
    
    printf("  Results:\n");
    printf("    Pages copied (COW faults): %d\n", pages_copied);
    printf("    +----------------------------------+\n");
    printf("    | COW write time:      %5d cycles |\n", (int)cow_time);
    printf("    | Standard write time: %5d cycles |\n", (int)std_time);
    printf("    | Overhead:            %5d cycles |\n", (int)(cow_time - std_time));
    printf("    +----------------------------------+\n");
    printf("    COW is slower due to page copy on write\n");
    
    /* 验证数据独立性 */
    printf("  Verifying data independence:\n");
    int parent_ok = 1, child_ok = 1;
    for (int i = 0; i < NUM_PAGES; i++) {
        int *parent_data = (int*)pages[i];
        int *child_data = (int*)child_pages[i];
        if (parent_data[0] != i * 100) parent_ok = 0;
        if (child_data[0] != i * 100 + 2) child_ok = 0;
    }
    printf("    Parent data intact: %s\n", parent_ok ? "YES" : "NO");
    printf("    Child data modified: %s\n", child_ok ? "YES" : "NO");
    
    assert(parent_ok && child_ok);
    assert(pages_copied == NUM_PAGES);
    
    /* 清理 */
    for (int i = 0; i < NUM_PAGES; i++) {
        page_decref((uint64)pages[i]);
        page_decref((uint64)child_pages[i]);
        free_page(pages[i]);
        free_page(child_pages[i]);
    }
    
    printf("  Trade-off: Slower writes, but faster fork() & less memory!\n");
    printf("Test 4 PASS!\n");
    
    #undef NUM_PAGES
}

/* ============== 测试5：内存效率演示 ============== */

static void test_memory_efficiency(void) {
    printf("\nTest 5: Memory Efficiency Demo\n");
    
    uint64 free_before = get_free_page_count();
    printf("  Free pages before: %lu\n", free_before);
    
    /* 分配一个大页面 */
    void *original = alloc_page();
    assert(original != 0);
    page_incref((uint64)original);
    
    /* 模拟 10 个进程 fork 但不写入 */
    printf("  Simulating 10 forks (read-only)...\n");
    for (int i = 0; i < 10; i++) {
        page_incref((uint64)original);
    }
    
    int ref = page_getref((uint64)original);
    printf("  Refcount after 10 forks: %d\n", ref);
    assert(ref == 11);
    
    uint64 free_after = get_free_page_count();
    printf("  Free pages after: %lu\n", free_after);
    
    /* 只有 1 个页面被使用，而不是 11 个 */
    assert(free_before - free_after == 1);
    
    /* 清理 */
    for (int i = 0; i < 11; i++) {
        page_decref((uint64)original);
    }
    free_page(original);
    
    printf("Test 5 PASS!\n");
}

/* ============== 主测试入口 ============== */

static void test_main(void) {
    test_refcount_basic();           /* T1: 引用计数基本功能 */
    test_multiple_pages();           /* T2: 多页引用计数 */
    test_cow_fork_simulation();      /* T3: COW Fork 模拟 */
    test_cow_write_performance();    /* T4: COW 写性能测试 */
    test_memory_efficiency();        /* T5: 内存效率 */
    
    printf("\n========================================\n");
    printf("    All tests PASSED!\n");
    printf("========================================\n");
}

/* ============== 内核入口 ============== */

void kernel_main(void) {
    printf("\n========================================\n");
    printf("    Exp8-5: Copy-on-Write Fork\n");
    printf("========================================\n");
    
    /* 初始化物理内存管理器 */
    pmm_init();
    
    /* 初始化 COW 系统 */
    cow_init();
    
    /* 直接运行测试 */
    test_main();
}
