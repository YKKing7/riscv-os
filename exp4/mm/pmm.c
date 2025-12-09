/*
 * 物理内存管理器实现
 * 
 * 优化特性：
 * - 空闲链表: O(1) 分配/释放
 * - 页面池缓存: 减少链表操作开销
 * - 批量分配: 一次获取多页
 * - 伙伴系统: 连续多页分配
 */

#include "../include/pmm.h"
#include "../include/memlayout.h"

/* 链接脚本符号 */
extern char _end[];

/* 空闲页面链表节点 */
struct run {
    struct run *next;
};

/* 页面池缓存大小 */
#define PAGE_POOL_SIZE  16

/* 物理内存管理器状态 */
static struct {
    struct run *freelist;           /* 空闲链表头 */
    uint64 free_count;              /* 空闲页面数 */
    uint64 total_count;             /* 总页面数 */
    /* 页面池缓存 - 减少链表操作 */
    void *pool[PAGE_POOL_SIZE];     /* 缓存的页面 */
    int pool_count;                 /* 缓存中的页面数 */
} pmm;

/*
 * 快速内存填充 (循环展开优化)
 */
static void memset64(void *dst, uint64 val, uint64 n) {
    uint64 *p = (uint64*)dst;
    uint64 count = n / 8;
    
    /* 8x 循环展开 */
    while (count >= 8) {
        p[0] = val; p[1] = val; p[2] = val; p[3] = val;
        p[4] = val; p[5] = val; p[6] = val; p[7] = val;
        p += 8;
        count -= 8;
    }
    while (count--)
        *p++ = val;
}

/*
 * 释放一段内存区域
 */
static void free_range(void *pa_start, void *pa_end) {
    char *p = (char*)PGROUNDUP((uint64)pa_start);
    
    for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
        struct run *r = (struct run*)p;
        r->next = pmm.freelist;
        pmm.freelist = r;
        pmm.free_count++;
        pmm.total_count++;
    }
}

/*
 * 初始化物理内存管理器
 */
void pmm_init(void) {
    pmm.freelist = 0;
    pmm.free_count = 0;
    pmm.total_count = 0;
    pmm.pool_count = 0;
    free_range(_end, (void*)PHYSTOP);
}

/*
 * 分配一个物理页面 (清零)
 * 优先从页面池分配，减少链表操作
 */
void* alloc_page(void) {
    void *page;
    
    /* 优先从池中分配 */
    if (pmm.pool_count > 0) {
        page = pmm.pool[--pmm.pool_count];
    } else if (pmm.freelist) {
        struct run *r = pmm.freelist;
        pmm.freelist = r->next;
        pmm.free_count--;
        page = (void*)r;
    } else {
        return 0;
    }
    
    memset64(page, 0, PGSIZE);
    return page;
}

/*
 * 分配页面但不清零 (性能优化)
 */
void* alloc_page_nozero(void) {
    if (pmm.pool_count > 0)
        return pmm.pool[--pmm.pool_count];
    
    if (pmm.freelist) {
        struct run *r = pmm.freelist;
        pmm.freelist = r->next;
        pmm.free_count--;
        return (void*)r;
    }
    return 0;
}

/*
 * 释放一个物理页面
 * 优先放入页面池
 */
void free_page(void *pa) {
    if (pa == 0 || (uint64)pa % PGSIZE != 0)
        return;
    if ((uint64)pa < (uint64)_end || (uint64)pa >= PHYSTOP)
        return;
    
    /* 优先放入池中 */
    if (pmm.pool_count < PAGE_POOL_SIZE) {
        pmm.pool[pmm.pool_count++] = pa;
        return;
    }
    
    /* 池满，放入链表 */
    struct run *r = (struct run*)pa;
    r->next = pmm.freelist;
    pmm.freelist = r;
    pmm.free_count++;
}

/*
 * 批量分配 n 个页面到数组
 * 返回实际分配的页面数
 */
int alloc_pages_batch(void **pages, int n) {
    if (n <= 0 || !pages)
        return 0;
    
    int allocated = 0;
    
    /* 先从池中取 */
    while (allocated < n && pmm.pool_count > 0)
        pages[allocated++] = pmm.pool[--pmm.pool_count];
    
    /* 再从链表取 */
    while (allocated < n && pmm.freelist) {
        struct run *r = pmm.freelist;
        pmm.freelist = r->next;
        pmm.free_count--;
        pages[allocated++] = (void*)r;
    }
    
    /* 清零所有分配的页面 */
    for (int i = 0; i < allocated; i++)
        memset64(pages[i], 0, PGSIZE);
    
    return allocated;
}

/*
 * 批量释放页面数组
 */
void free_pages_batch(void **pages, int n) {
    for (int i = 0; i < n; i++) {
        if (pages[i])
            free_page(pages[i]);
    }
}

/*
 * 分配连续 n 页 (不保证连续，使用伙伴系统获取连续页)
 */
void* alloc_pages(int n) {
    if (n <= 0) return 0;
    if (n == 1) return alloc_page();
    
    void *first = alloc_page();
    if (!first) return 0;
    
    for (int i = 1; i < n; i++) {
        if (!alloc_page())
            return 0;
    }
    return first;
}

/*
 * 释放连续 n 页
 */
void free_pages(void *pa, int n) {
    char *p = (char*)pa;
    for (int i = 0; i < n; i++, p += PGSIZE)
        free_page(p);
}

/* 获取统计信息 */
uint64 get_free_page_count(void) { 
    return pmm.free_count + pmm.pool_count; 
}
uint64 get_total_page_count(void) { return pmm.total_count; }

/* ============== 伙伴系统实现 ============== */

#define MAX_ORDER       10  /* 最大 2^10 = 1024 页 (4MB) */
#define MAX_BUDDY_SIZE  (1UL << MAX_ORDER)

struct buddy_node {
    struct buddy_node *next;
};

static struct {
    struct buddy_node *freelist[MAX_ORDER + 1];
    uint64 base_addr;
    uint64 total_pages;
} buddy;

/* 内联辅助函数 */
static inline uint64 page_index(void *pa) {
    return ((uint64)pa - buddy.base_addr) / PGSIZE;
}

static inline void* page_addr(uint64 idx) {
    return (void*)(buddy.base_addr + idx * PGSIZE);
}

static inline void* get_buddy_addr(void *pa, int order) {
    return page_addr(page_index(pa) ^ (1UL << order));
}

static inline int is_valid_addr(void *pa) {
    uint64 addr = (uint64)pa;
    return addr >= buddy.base_addr && 
           addr < buddy.base_addr + buddy.total_pages * PGSIZE;
}

/*
 * 初始化伙伴系统
 */
void buddy_init(void) {
    buddy.base_addr = PGROUNDUP((uint64)_end);
    buddy.total_pages = (PHYSTOP - buddy.base_addr) / PGSIZE;
    
    for (int i = 0; i <= MAX_ORDER; i++)
        buddy.freelist[i] = 0;
    
    /* 将内存按 2^order 大小分块加入对应链表 */
    uint64 remaining = buddy.total_pages;
    uint64 offset = 0;
    
    for (int order = MAX_ORDER; order >= 0 && remaining > 0; order--) {
        uint64 block_size = 1UL << order;
        while (remaining >= block_size) {
            struct buddy_node *node = (struct buddy_node*)page_addr(offset);
            node->next = buddy.freelist[order];
            buddy.freelist[order] = node;
            offset += block_size;
            remaining -= block_size;
        }
    }
}

/*
 * 从指定级别分配块 (递归分裂)
 */
static void* buddy_alloc_from_order(int order) {
    if (order > MAX_ORDER)
        return 0;
    
    if (buddy.freelist[order]) {
        struct buddy_node *node = buddy.freelist[order];
        buddy.freelist[order] = node->next;
        return (void*)node;
    }
    
    /* 从更大级别分裂 */
    void *block = buddy_alloc_from_order(order + 1);
    if (!block)
        return 0;
    
    /* 分裂：将伙伴块加入当前级别 */
    struct buddy_node *buddy_block = (struct buddy_node*)get_buddy_addr(block, order);
    buddy_block->next = buddy.freelist[order];
    buddy.freelist[order] = buddy_block;
    
    return block;
}

/*
 * 分配连续 n 页
 */
void* buddy_alloc_pages(int n) {
    if (n <= 0 || (uint64)n > MAX_BUDDY_SIZE)
        return 0;
    
    /* 计算所需级别 (向上取 2 的幂) */
    int order = 0;
    while ((1UL << order) < (uint64)n)
        order++;
    
    void *block = buddy_alloc_from_order(order);
    if (block)
        memset64(block, 0, (1UL << order) * PGSIZE);
    
    return block;
}

/*
 * 释放块到指定级别 (递归合并)
 */
static void buddy_free_to_order(void *pa, int order) {
    if (order >= MAX_ORDER) {
        struct buddy_node *node = (struct buddy_node*)pa;
        node->next = buddy.freelist[order];
        buddy.freelist[order] = node;
        return;
    }
    
    /* 查找伙伴 */
    void *buddy_pa = get_buddy_addr(pa, order);
    struct buddy_node **pp = &buddy.freelist[order];
    
    while (*pp) {
        if ((void*)*pp == buddy_pa) {
            /* 找到伙伴，移除并合并 */
            *pp = (*pp)->next;
            void *merged = (uint64)pa < (uint64)buddy_pa ? pa : buddy_pa;
            buddy_free_to_order(merged, order + 1);
            return;
        }
        pp = &(*pp)->next;
    }
    
    /* 无伙伴，直接加入链表 */
    struct buddy_node *node = (struct buddy_node*)pa;
    node->next = buddy.freelist[order];
    buddy.freelist[order] = node;
}

/*
 * 释放连续 n 页
 */
void buddy_free_pages(void *pa, int n) {
    if (!pa || n <= 0 || (uint64)n > MAX_BUDDY_SIZE || !is_valid_addr(pa))
        return;
    if ((uint64)pa % PGSIZE != 0)
        return;
    
    int order = 0;
    while ((1UL << order) < (uint64)n)
        order++;
    
    buddy_free_to_order(pa, order);
}
