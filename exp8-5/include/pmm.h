/*
 * 物理内存管理器头文件
 */

#ifndef PMM_H
#define PMM_H

#include "types.h"

/* ============== 基本接口 ============== */

/* 初始化物理内存管理器 */
void pmm_init(void);

/* 分配一个物理页面 (清零) */
void* alloc_page(void);

/* 分配一个物理页面 (不清零，性能优化) */
void* alloc_page_nozero(void);

/* 释放一个物理页面 */
void free_page(void* pa);

/* 分配连续 n 页 (不保证物理连续) */
void* alloc_pages(int n);

/* 释放连续 n 页 */
void free_pages(void* pa, int n);

/* ============== 批量分配接口 ============== */

/* 批量分配 n 页到数组，返回实际分配数 */
int alloc_pages_batch(void **pages, int n);

/* 批量释放页面数组 */
void free_pages_batch(void **pages, int n);

/* ============== 统计接口 ============== */

uint64 get_free_page_count(void);
uint64 get_total_page_count(void);

/* ============== 伙伴系统接口 ============== */

/* 初始化伙伴系统 */
void buddy_init(void);

/* 分配连续 n 页 (物理连续) */
void* buddy_alloc_pages(int n);

/* 释放连续 n 页 */
void buddy_free_pages(void* pa, int n);

#endif /* PMM_H */
