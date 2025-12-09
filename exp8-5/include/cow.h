/*
 * cow.h - Copy-on-Write (COW) Fork 支持
 * 
 * 扩展实验8-5: COW Fork
 *   - 物理页引用计数管理
 *   - COW 页面分配与处理
 *   - 页表 COW 标记操作
 */

#ifndef COW_H
#define COW_H

#include "types.h"

/* ============== 内存布局常量 ============== */

#define PGSIZE      4096            /* 页大小 */
#define KERNBASE    0x80000000UL    /* 内核起始地址 */
#define PHYSTOP     0x88000000UL    /* 物理内存上限 (128MB) */
#define MAXPAGES    ((PHYSTOP - KERNBASE) / PGSIZE)

/* ============== 接口函数 ============== */

/* 初始化 COW 引用计数系统 */
void cow_init(void);

/* 增加物理页引用计数 */
void page_incref(uint64 pa);

/* 减少物理页引用计数，返回新的计数值 */
int page_decref(uint64 pa);

/* 获取物理页引用计数 */
int page_getref(uint64 pa);

/* COW 页面分配：处理写时复制缺页 */
int cow_alloc(uint64 *pagetable, uint64 va);

/* COW 版本的 uvmcopy：fork 时使用 */
int uvmcopy_cow(uint64 *old, uint64 *new, uint64 sz);

/* 检查页表项是否为 COW 页 */
int is_cow_page(uint64 *pagetable, uint64 va);

/* 获取 COW 统计信息 */
struct cow_stats {
    uint64 total_pages;     /* 总物理页数 */
    uint64 shared_pages;    /* 共享页数 (refcnt > 1) */
    uint64 cow_faults;      /* COW 缺页次数 */
    uint64 pages_copied;    /* 实际复制的页数 */
};

struct cow_stats cow_get_stats(void);
void cow_reset_stats(void);

#endif /* COW_H */
