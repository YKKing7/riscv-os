/*
 * cow.c - Copy-on-Write (COW) Fork 实现
 * 
 * 扩展实验8-5: COW Fork
 *   - 物理页引用计数管理
 *   - COW 缺页处理
 *   - uvmcopy COW 版本
 */

#include "../include/cow.h"
#include "../include/riscv.h"
#include "../include/pmm.h"
#include "../include/printf.h"

/* ============== 全局变量 ============== */

/* 物理页引用计数数组 */
static int page_refcnt[MAXPAGES];

/* 简单自旋锁 */
static volatile int cow_lock = 0;

/* 统计信息 */
static struct cow_stats stats = {0};

/* ============== 自旋锁 ============== */

static void acquire_lock(void) {
    while (cow_lock)
        ;
    cow_lock = 1;
}

static void release_lock(void) {
    cow_lock = 0;
}

/* ============== 辅助函数 ============== */

/* 物理地址转页索引 */
static inline int pa_to_index(uint64 pa) {
    if (pa < KERNBASE || pa >= PHYSTOP)
        return -1;
    return (pa - KERNBASE) / PGSIZE;
}

/* 页索引转物理地址 */
static inline uint64 index_to_pa(int idx) {
    return KERNBASE + (uint64)idx * PGSIZE;
}

/* 简单内存复制 */
static void memcpy_page(void *dst, const void *src) {
    uint64 *d = (uint64*)dst;
    const uint64 *s = (const uint64*)src;
    for (int i = 0; i < PGSIZE / sizeof(uint64); i++)
        d[i] = s[i];
}

/* ============== 页表操作辅助函数 ============== */

/* 遍历页表获取 PTE 指针 */
static uint64* walk(uint64 *pagetable, uint64 va, int alloc) {
    for (int level = 2; level > 0; level--) {
        uint64 *pte = &pagetable[VA_VPN(va, level)];
        if (*pte & PTE_V) {
            pagetable = (uint64*)PTE2PA(*pte);
        } else {
            if (!alloc)
                return 0;
            pagetable = (uint64*)alloc_page();
            if (pagetable == 0)
                return 0;
            /* 清零新页表 */
            for (int i = 0; i < 512; i++)
                pagetable[i] = 0;
            *pte = PA2PTE((uint64)pagetable) | PTE_V;
        }
    }
    return &pagetable[VA_VPN(va, 0)];
}

/* ============== 引用计数管理 ============== */

void cow_init(void) {
    acquire_lock();
    for (int i = 0; i < MAXPAGES; i++) {
        page_refcnt[i] = 0;
    }
    stats.total_pages = MAXPAGES;
    stats.shared_pages = 0;
    stats.cow_faults = 0;
    stats.pages_copied = 0;
    release_lock();
}

void page_incref(uint64 pa) {
    int idx = pa_to_index(pa);
    if (idx < 0) return;
    
    acquire_lock();
    page_refcnt[idx]++;
    release_lock();
}

int page_decref(uint64 pa) {
    int idx = pa_to_index(pa);
    if (idx < 0) return -1;
    
    acquire_lock();
    int newref = --page_refcnt[idx];
    release_lock();
    return newref;
}

int page_getref(uint64 pa) {
    int idx = pa_to_index(pa);
    if (idx < 0) return 0;
    
    acquire_lock();
    int ref = page_refcnt[idx];
    release_lock();
    return ref;
}

/* ============== COW 核心实现 ============== */

/* 检查是否为 COW 页 */
int is_cow_page(uint64 *pagetable, uint64 va) {
    uint64 *pte = walk(pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
        return 0;
    return (*pte & PTE_C) != 0;
}

/* COW 缺页处理 */
int cow_alloc(uint64 *pagetable, uint64 va) {
    va = va & ~(PGSIZE - 1);  /* 页对齐 */
    
    uint64 *pte = walk(pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_V) == 0) {
        return -1;  /* 无效地址 */
    }
    
    /* 检查是否为 COW 页 */
    if ((*pte & PTE_C) == 0) {
        return -1;  /* 不是 COW 页，非法写入 */
    }
    
    uint64 old_pa = PTE2PA(*pte);
    uint64 flags = PTE_FLAGS(*pte);
    
    acquire_lock();
    stats.cow_faults++;
    int refcnt = page_refcnt[pa_to_index(old_pa)];
    release_lock();
    
    if (refcnt == 1) {
        /* 只有一个引用，直接修改权限即可 */
        *pte = PA2PTE(old_pa) | (flags & ~PTE_C) | PTE_W;
        sfence_vma();
        return 0;
    }
    
    /* 多个引用，需要复制页面 */
    void *new_page = alloc_page();
    if (new_page == 0) {
        return -1;  /* 内存不足 */
    }
    
    /* 复制数据 */
    memcpy_page(new_page, (void*)old_pa);
    
    /* 更新页表：指向新页，设置可写，清除 COW 标志 */
    *pte = PA2PTE((uint64)new_page) | (flags & ~PTE_C) | PTE_W;
    
    /* 新页引用计数设为 1 */
    page_incref((uint64)new_page);
    
    /* 减少旧页引用计数 */
    int old_ref = page_decref(old_pa);
    if (old_ref == 0) {
        /* 旧页不再被使用，释放 */
        free_page((void*)old_pa);
    }
    
    acquire_lock();
    stats.pages_copied++;
    release_lock();
    
    sfence_vma();
    return 0;
}

/* COW 版本的 uvmcopy */
int uvmcopy_cow(uint64 *old, uint64 *new, uint64 sz) {
    uint64 i;
    uint64 *pte;
    uint64 pa;
    uint64 flags;
    
    for (i = 0; i < sz; i += PGSIZE) {
        pte = walk(old, i, 0);
        if (pte == 0)
            continue;
        if ((*pte & PTE_V) == 0)
            continue;
        
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        
        /* 如果页面可写，或已经是 COW 页 */
        if ((flags & PTE_W) || (flags & PTE_C)) {
            /* 清除写权限，设置 COW 标志 */
            flags = (flags & ~PTE_W) | PTE_C;
            /* 更新父进程页表 */
            *pte = PA2PTE(pa) | flags;
        }
        
        /* 增加物理页引用计数 */
        page_incref(pa);
        
        /* 在子进程页表中建立相同映射 */
        uint64 *new_pte = walk(new, i, 1);
        if (new_pte == 0) {
            page_decref(pa);
            return -1;
        }
        *new_pte = PA2PTE(pa) | flags;
    }
    
    sfence_vma();
    return 0;
}

/* ============== 统计信息 ============== */

struct cow_stats cow_get_stats(void) {
    struct cow_stats s;
    acquire_lock();
    s = stats;
    
    /* 计算共享页数 */
    s.shared_pages = 0;
    for (int i = 0; i < MAXPAGES; i++) {
        if (page_refcnt[i] > 1)
            s.shared_pages++;
    }
    release_lock();
    return s;
}

void cow_reset_stats(void) {
    acquire_lock();
    stats.cow_faults = 0;
    stats.pages_copied = 0;
    release_lock();
}
