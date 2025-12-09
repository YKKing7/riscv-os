/*
 * 实验3：物理内存管理与虚拟内存
 * 
 * 实验目标：
 *   1. 实现物理内存分配器 (空闲链表)
 *   2. 实现 Sv39 页表管理
 *   3. 启用虚拟内存
 *   4. 实现伙伴系统
 * 
 * 基于实验2，新增：
 *   - include/memlayout.h : 内存布局定义
 *   - include/riscv.h     : RISC-V 特权级相关定义
 *   - include/pmm.h       : 物理内存管理接口
 *   - include/vm.h        : 虚拟内存管理接口
 *   - mm/pmm.c            : 物理内存管理实现
 *   - mm/vm.c             : 虚拟内存管理实现
 */

#include "include/uart.h"
#include "include/printf.h"
#include "include/types.h"
#include "include/memlayout.h"
#include "include/riscv.h"
#include "include/pmm.h"
#include "include/vm.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

static inline uint64 rdcycle(void) {
    uint64 c;
    __asm__ volatile("rdcycle %0" : "=r"(c));
    return c;
}

/* ============== 功能测试 ============== */

/* 测试1: 物理内存分配器 */
void test_pmm(void) {
    printf("\nTest 1: Physical Memory Manager\n");
    
    void *p1 = alloc_page(), *p2 = alloc_page();
    assert(p1 && p2 && p1 != p2);
    assert(((uint64)p1 & 0xFFF) == 0);  /* 页对齐 */
    
    *(int*)p1 = 0x12345678;
    assert(*(int*)p1 == 0x12345678);
    
    free_page(p1);
    free_page(p2);
    printf("Test 1 PASS!\n");
}

/* 测试2: 页表管理 */
void test_pagetable(void) {
    printf("\nTest 2: Page Table Management\n");
    
    pagetable_t pt = create_pagetable();
    uint64 va = 0x1000000, pa = (uint64)alloc_page();
    
    assert(map_page(pt, va, pa, PTE_R | PTE_W) == 0);
    
    pte_t *pte = walk_lookup(pt, va);
    assert(pte && (*pte & PTE_V));
    assert(PTE_PA(*pte) == pa);
    assert((*pte & PTE_R) && (*pte & PTE_W) && !(*pte & PTE_X));
    
    free_page((void*)pa);
    destroy_pagetable(pt);
    printf("Test 2 PASS!\n");
}

/* 测试3: 虚拟内存激活 */
void test_vm(void) {
    printf("\nTest 3: Virtual Memory Activation\n");
    
    kvminit();
    kvminithart();
    
    /* 验证代码可执行 */
    void (*fn)(void) = test_vm;
    assert(fn != 0);
    
    /* 验证数据可访问 */
    void *p = alloc_page();
    assert(p != 0);
    *(volatile uint64*)p = 0xCAFEBABE;
    assert(*(volatile uint64*)p == 0xCAFEBABE);
    free_page(p);
    
    /* 验证设备可访问 */
    (void)*(volatile uint32*)UART0;
    
    printf("Test 3 PASS!\n");
}

/* 测试4: 伙伴系统 */
void test_buddy(void) {
    printf("\nTest 4: Buddy System\n");
    
    buddy_init();
    
    void *p1 = buddy_alloc_pages(1);
    void *p4 = buddy_alloc_pages(4);
    void *p2 = buddy_alloc_pages(2);
    void *p8 = buddy_alloc_pages(8);
    
    assert(p1 && p4 && p2 && p8);
    assert(((uint64)p4 & 0xFFF) == 0);
    
    /* 验证连续性 */
    for (int i = 0; i < 4; i++)
        *(int*)((char*)p4 + i * PGSIZE) = i;
    for (int i = 0; i < 4; i++)
        assert(*(int*)((char*)p4 + i * PGSIZE) == i);
    
    buddy_free_pages(p4, 4);
    void *p4_new = buddy_alloc_pages(4);
    assert(p4_new != 0);
    
    buddy_free_pages(p1, 1);
    buddy_free_pages(p2, 2);
    buddy_free_pages(p8, 8);
    buddy_free_pages(p4_new, 4);
    
    printf("Test 4 PASS!\n");
}

/* ============== 性能测试 ============== */

/* 测试5: 内存分配性能 */
void test_alloc_perf(void) {
    printf("\nTest 5: Allocation Performance\n");
    
    #define N 50
    void *pages[N];
    uint64 t0, t1;
    
    /* Opt1: 64位 memset vs 逐字节 */
    printf("  [Opt 1] 64-bit vs byte memset\n");
    void *pg = alloc_page_nozero();
    
    t0 = rdcycle();
    for (int r = 0; r < 50; r++) {
        volatile char *p = (volatile char*)pg;
        for (int i = 0; i < PGSIZE; i++) p[i] = 0;
    }
    t1 = rdcycle();
    uint64 byte_t = (t1 - t0) / 50;
    
    t0 = rdcycle();
    for (int r = 0; r < 50; r++) {
        volatile uint64 *p = (volatile uint64*)pg;
        for (int i = 0; i < PGSIZE/8; i++) p[i] = 0;
    }
    t1 = rdcycle();
    uint64 word_t = (t1 - t0) / 50;
    
    printf("    Byte-by-byte: %ld cycles/page\n", byte_t);
    printf("    64-bit word:  %ld cycles/page\n", word_t);
    printf("    >>> Speedup:  %ld%%\n", (byte_t - word_t) * 100 / byte_t);
    free_page(pg);
    
    /* Opt2: 不清零分配 */
    printf("  [Opt 2] No-zero allocation\n");
    t0 = rdcycle();
    for (int i = 0; i < N; i++) pages[i] = alloc_page_nozero();
    t1 = rdcycle();
    uint64 alloc_t = (t1 - t0) / N;
    for (int i = 0; i < N; i++) free_page(pages[i]);
    
    uint64 total_t = alloc_t + word_t;
    printf("    Alloc only:     %ld cycles/page\n", alloc_t);
    printf("    Alloc + memset: %ld cycles/page\n", total_t);
    printf("    >>> Speedup:    %ld%%\n", word_t * 100 / total_t);
    
    /* Opt3: 伙伴系统批量分配 */
    printf("  [Opt 3] Buddy vs single (8 pages)\n");
    t0 = rdcycle();
    for (int i = 0; i < 8; i++) pages[i] = alloc_page();
    t1 = rdcycle();
    uint64 single_t = t1 - t0;
    for (int i = 0; i < 8; i++) free_page(pages[i]);
    
    t0 = rdcycle();
    void *blk = buddy_alloc_pages(8);
    t1 = rdcycle();
    uint64 buddy_t = t1 - t0;
    buddy_free_pages(blk, 8);
    
    printf("    Single x8:  %ld cycles\n", single_t);
    printf("    Buddy(8):   %ld cycles\n", buddy_t);
    printf("    >>> Speedup: %ld%%\n", (single_t - buddy_t) * 100 / single_t);
    
    printf("Test 5 PASS!\n");
    #undef N
}

/* 测试6: 大页映射性能 */
void test_hugepage_perf(void) {
    printf("\nTest 6: Huge Page Performance\n");
    
    #define SZ (8 * 1024 * 1024)
    uint64 va = 0x40000000UL, pa = 0x82000000UL;
    uint64 t0, t1;
    
    pagetable_t pt1 = create_pagetable();
    pagetable_t pt2 = create_pagetable();
    
    /* 4KB 页映射 */
    t0 = rdcycle();
    for (uint64 off = 0; off < SZ; off += PGSIZE)
        map_page(pt1, va + off, pa + off, PTE_R | PTE_W);
    t1 = rdcycle();
    uint64 small_t = t1 - t0;
    
    /* 2MB 大页映射 */
    t0 = rdcycle();
    map_region(pt2, va, pa, SZ, PTE_R | PTE_W);
    t1 = rdcycle();
    uint64 huge_t = t1 - t0;
    
    printf("  4KB: %d entries, %ld cycles\n", SZ/PGSIZE, small_t);
    printf("  2MB: %d entries, %ld cycles\n", SZ/(2*1024*1024), huge_t);
    printf("  >>> Speedup: %ld%%\n", (small_t - huge_t) * 100 / small_t);
    
    destroy_pagetable(pt1);
    destroy_pagetable(pt2);
    printf("Test 6 PASS!\n");
    #undef SZ
}

/* ============== 内核入口 ============== */

void kernel_main(void) {
    uart_init();
    
    printf("\n=============================================\n");
    printf("  Exp3: Physical & Virtual Memory Management\n");
    printf("=============================================\n");
    
    pmm_init();
    
    /* 功能测试 */
    test_pmm();
    test_pagetable();
    test_vm();
    test_buddy();
    
    /* 性能测试 */
    test_alloc_perf();
    test_hugepage_perf();
    
    printf("\n=============================================\n");
    printf("  All tests passed!\n");
    printf("=============================================\n");
    
    while (1) __asm__ volatile("wfi");
}
