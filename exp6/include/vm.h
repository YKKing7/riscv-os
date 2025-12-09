/*
 * 虚拟内存管理头文件
 * 功能：页表创建、映射、地址转换接口
 */

#ifndef VM_H
#define VM_H

#include "types.h"
#include "riscv.h"

/* 页表类型定义 */
typedef uint64* pagetable_t;

/* 页表项类型 */
typedef uint64 pte_t;

/* ============== 页表管理接口 ============== */

/* 创建一个新的空页表，返回页表地址，失败返回 0 */
pagetable_t create_pagetable(void);

/* 销毁页表，释放所有相关页面 */
void destroy_pagetable(pagetable_t pt);

/* 建立单页映射 (4KB) */
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm);

/* 建立大页映射 (2MB) - va/pa 必须 2MB 对齐 */
int map_megapage(pagetable_t pt, uint64 va, uint64 pa, int perm);

/* 建立区域映射 (自动选择大页或普通页) */
int map_region(pagetable_t pt, uint64 va, uint64 pa, uint64 size, int perm);

/* 取消单页映射 */
int unmap_page(pagetable_t pt, uint64 va);

/* ============== 页表遍历接口 ============== */

/*
 * 遍历页表，查找虚拟地址对应的 PTE
 * 如果中间页表不存在，根据 alloc 参数决定是否创建
 * pt:    页表
 * va:    虚拟地址
 * alloc: 是否创建缺失的中间页表
 * 返回：PTE 指针，失败返回 0
 */
pte_t* walk(pagetable_t pt, uint64 va, int alloc);

/* 查找 PTE（不创建中间页表） */
pte_t* walk_lookup(pagetable_t pt, uint64 va);

/* 查找或创建 PTE */
pte_t* walk_create(pagetable_t pt, uint64 va);

/* ============== 地址转换接口 ============== */

/*
 * 虚拟地址转物理地址
 * pt: 页表
 * va: 虚拟地址
 * 返回：物理地址，失败返回 0
 */
uint64 va2pa(pagetable_t pt, uint64 va);

/* ============== 内核页表管理 ============== */

/* 内核页表（全局变量） */
extern pagetable_t kernel_pagetable;

/* 初始化内核页表 */
void kvminit(void);

/* 激活内核页表（设置 satp 寄存器） */
void kvminithart(void);

#endif /* VM_H */
